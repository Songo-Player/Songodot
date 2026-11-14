#include "gdtaglib.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif

// ====================================================================
// Native directory walk (scan_directories)
// ====================================================================

namespace {

// How often (in matched files) the optional progress callback fires.
const int SCAN_PROGRESS_EVERY = 64;

struct ScanState {
	// lowercase extension -> output group index
	std::unordered_map<std::string, int> ext_to_group;
	std::vector<PackedStringArray> groups;
	// Per group: whether to record mtime / size, and the parallel arrays.
	std::vector<bool> want_stamps;
	std::vector<PackedInt64Array> mtimes;
	std::vector<PackedInt64Array> sizes;
	Callable progress;
	int64_t matched = 0;
#ifndef _WIN32
	// (device, inode) of every directory entered, so symlink loops and
	// directories reachable twice are only walked once.
	std::set<std::pair<dev_t, ino_t>> visited;
#else
	std::set<String> visited;
#endif

	// Output group for a file name, or -1 when no group wants it.
	int group_of(const String &p_name) const {
		auto it = ext_to_group.find(p_name.get_extension().to_lower().utf8().get_data());
		return it == ext_to_group.end() ? -1 : it->second;
	}

	// p_mtime / p_size are only stored for groups that asked for stamps;
	// 0 / -1 mean unknown (same convention as TagLibMusicRecord).
	void add_file(int p_group, const String &p_out_path, int64_t p_mtime, int64_t p_size) {
		groups[p_group].push_back(p_out_path);
		if (want_stamps[p_group]) {
			mtimes[p_group].push_back(p_mtime);
			sizes[p_group].push_back(p_size);
		}
		matched++;
		if (progress.is_valid() && matched % SCAN_PROGRESS_EVERY == 0) {
			progress.call(matched);
		}
	}
};

// Entries are visited in sorted order so the result (and therefore the
// first-seen naming in album assembly) is the same on every scan.
struct ScanEntry {
	String name;
	bool is_dir = false;
	std::string raw; // on-disk bytes, which needn't be valid UTF-8
	bool operator<(const ScanEntry &p_other) const { return name < p_other.name; }
};

#ifndef _WIN32

void _scan_dir(ScanState &s, const std::string &p_fs_path, const String &p_out_path) {
	struct stat dir_st;
	if (stat(p_fs_path.c_str(), &dir_st) != 0 || !S_ISDIR(dir_st.st_mode)) {
		return;
	}
	if (!s.visited.insert({ dir_st.st_dev, dir_st.st_ino }).second) {
		return;
	}

	DIR *dir = opendir(p_fs_path.c_str());
	if (!dir) {
		WARN_PRINT("GDTagLib: can't open directory '" + p_out_path + "'.");
		return;
	}

	std::vector<ScanEntry> entries;
	std::string base = p_fs_path;
	if (base.empty() || base.back() != '/') {
		base += '/';
	}
	while (struct dirent *e = readdir(dir)) {
		// Skips ".", ".." and hidden entries, like DirAccess does by default.
		if (e->d_name[0] == '.') {
			continue;
		}
		bool is_dir = false;
		bool is_file = false;
		// d_type saves a stat() per entry; fall back to stat() where the
		// filesystem doesn't fill it in, and to follow symlinks.
		if (e->d_type == DT_DIR) {
			is_dir = true;
		} else if (e->d_type == DT_REG) {
			is_file = true;
		} else {
			struct stat st;
			std::string child = base + e->d_name;
			if (stat(child.c_str(), &st) == 0) {
				is_dir = S_ISDIR(st.st_mode);
				is_file = S_ISREG(st.st_mode);
			}
		}
		if (is_dir || is_file) {
			entries.push_back({ String::utf8(e->d_name), is_dir, e->d_name });
		}
	}

	// The directory stays open through the loop so stamps come from
	// fstatat() relative to it, with no second walk of the full path.
	int dfd = dirfd(dir);
	std::sort(entries.begin(), entries.end());
	for (const ScanEntry &entry : entries) {
		String out_child = p_out_path.path_join(entry.name);
		if (entry.is_dir) {
			_scan_dir(s, base + entry.raw, out_child);
			continue;
		}
		int group = s.group_of(entry.name);
		if (group < 0) {
			continue;
		}
		int64_t mtime = 0;
		int64_t size = -1;
		struct stat st;
		if (s.want_stamps[group] && fstatat(dfd, entry.raw.c_str(), &st, 0) == 0) {
			mtime = (int64_t)st.st_mtime;
			size = (int64_t)st.st_size;
		}
		s.add_file(group, out_child, mtime, size);
	}
	closedir(dir);
}

#else

// Windows isn't a shipping target; walk through DirAccess so it still works.
void _scan_dir(ScanState &s, const String &p_fs_path, const String &p_out_path) {
	if (!s.visited.insert(p_fs_path.simplify_path()).second) {
		return;
	}
	Ref<DirAccess> dir = DirAccess::open(p_fs_path);
	if (dir.is_null()) {
		WARN_PRINT("GDTagLib: can't open directory '" + p_out_path + "'.");
		return;
	}
	std::vector<ScanEntry> entries;
	for (const String &name : dir->get_directories()) {
		entries.push_back({ name, true });
	}
	for (const String &name : dir->get_files()) {
		entries.push_back({ name, false });
	}
	std::sort(entries.begin(), entries.end());
	for (const ScanEntry &entry : entries) {
		String out_child = p_out_path.path_join(entry.name);
		if (entry.is_dir) {
			_scan_dir(s, p_fs_path.path_join(entry.name), out_child);
			continue;
		}
		int group = s.group_of(entry.name);
		if (group >= 0) {
			// Stamps unknown here; TagLibLibrary.plan_update stats instead.
			s.add_file(group, out_child, 0, -1);
		}
	}
}

#endif

} // namespace

Dictionary GDTagLib::scan_directories(const PackedStringArray &p_roots, const Dictionary &p_groups,
		const Callable &p_progress, const Dictionary &p_opts) {
	ScanState s;
	s.progress = p_progress;

	PackedStringArray stamp_groups = p_opts.get("stamps", PackedStringArray());
	Array group_names = p_groups.keys();
	for (int g = 0; g < group_names.size(); g++) {
		String group_name = group_names[g];
		s.groups.push_back(PackedStringArray());
		s.want_stamps.push_back(stamp_groups.has(group_name));
		s.mtimes.push_back(PackedInt64Array());
		s.sizes.push_back(PackedInt64Array());
		PackedStringArray exts = p_groups[group_names[g]];
		for (const String &raw_ext : exts) {
			String ext = raw_ext.to_lower();
			if (ext.begins_with(".")) {
				ext = ext.substr(1);
			}
			s.ext_to_group[ext.utf8().get_data()] = g;
		}
	}

	for (const String &root : p_roots) {
		if (root.is_empty()) {
			continue;
		}
		String fs_root = ProjectSettings::get_singleton()->globalize_path(root);
#ifndef _WIN32
		_scan_dir(s, std::string(fs_root.utf8().get_data()), root);
#else
		_scan_dir(s, fs_root, root);
#endif
	}

	if (p_progress.is_valid()) {
		p_progress.call(s.matched);
	}

	Dictionary out;
	for (int g = 0; g < group_names.size(); g++) {
		String name = group_names[g];
		out[name] = s.groups[g];
		if (s.want_stamps[g]) {
			out[name + "_mtimes"] = s.mtimes[g];
			out[name + "_sizes"] = s.sizes[g];
		}
	}
	return out;
}
