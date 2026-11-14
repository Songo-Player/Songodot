#include "register_types.h"

#include "gdtaglib.h"
#include "taglib_records.h"

// Module-owned GDTagLib instance. It only hosts signals; the constructor
// claims the static singleton slot.
static GDTagLib *gdtaglib_singleton = nullptr;

void initialize_gdtaglib_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	// Records first: the library and GDTagLib reference them.
	GDREGISTER_CLASS(TagLibMusicRecord);
	GDREGISTER_ABSTRACT_CLASS(TagLibTrackGroup);
	GDREGISTER_CLASS(TagLibAlbumRecord);
	GDREGISTER_CLASS(TagLibArtistRecord);
	GDREGISTER_CLASS(TagLibTrackList);
	GDREGISTER_CLASS(TagLibLibrary);
	GDREGISTER_CLASS(GDTagLib);

	gdtaglib_singleton = memnew(GDTagLib);
}

void uninitialize_gdtaglib_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	if (gdtaglib_singleton) {
		memdelete(gdtaglib_singleton);
		gdtaglib_singleton = nullptr;
	}
}
