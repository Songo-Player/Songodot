# Build profile for Songodot Android export templates, tuned for size like
# sbc_profile.py. Keep the two in sync when Songo's needs change.
#
# Usage (needs ANDROID_HOME and a JDK 17 for the Gradle step):
#   JAVA_HOME=~/Android/jdk17 scons profile=android_profile.py -j 16
#
# Any option given on the command line overrides the value here, e.g. the
# debug template used for one-click deploy / debug exports:
#   JAVA_HOME=~/Android/jdk17 scons profile=android_profile.py target=template_debug -j 16
#
# With generate_apk, Gradle then writes bin/android_source.zip (what Songo-5's
# Gradle build export installs into res://android/build) and
# bin/android_release.apk / android_debug.apk for non-Gradle exports.
#
# Tuned for Songo (../Songo-5): a 2D, UI-only app. Music playback goes through
# the songo_player_harness GDExtension, so most engine codecs are unnecessary.

platform = "android"
# Songo-5's Android export only enables arm64-v8a, and the songo_player_harness
# extension only ships arm64 Android libraries.
arch = "arm64"
target = "template_release"

progress = "yes"
generate_apk = "yes"

# --- Renderers ---------------------------------------------------------------
# Songo-5 sets no renderer, so Android uses the Mobile (Vulkan) renderer and
# falls back to Compatibility (GLES3) on devices without Vulkan. Keep both.
vulkan = "yes"
opengl3 = "yes"

# --- Size ----------------------------------------------------------------------
optimize = "size"
debug_symbols = "no"
# Android's lto=auto means none, so this is explicit. Linking is slow and RAM
# hungry; "thin" is a middle ground while iterating.
lto = "full"
# Same extra size flags platform/sbc/detect.py adds (Android's detect.py
# already has -ffunction-sections and --gc-sections): separate data sections
# for --gc-sections, and identical code folding in lld (the NDK's linker).
ccflags = "-fdata-sections"
linkflags = "-Wl,--icf=all"

deprecated = "no"  # Compat code for removed APIs; project and GDExtensions target 4.3.
minizip = "no"  # ZIPReader/ZIPPacker and .zip resource packs; themes/plugins use .pck.
# Keep Brotli: it's needed to decode WOFF2 fonts, and Godot's built-in fallback
# font (used by any control a theme doesn't give a font, e.g. LineEdit) is WOFF2.
brotli = "yes"
openxr = "no"
builtin_pcre2_with_jit = "no"  # RegEx still works, just without the JIT.

# Nodes/resources: 3D (incl. all 3D lighting, GI, lightmaps, physics, navigation).
disable_3d = "yes"
# Kept enabled so third-party theme/plugin .pck files can use RichTextLabel,
# OptionButton, PopupMenu, TextEdit, etc.
disable_advanced_gui = "no"
# Same unregistered 2D classes as the SBC build (physics, 2D lights,
# particles, tilemaps, navigation, skeletons, parallax). The list isn't
# SBC specific; it's whatever Songo and its themes don't use.
build_profile = "sbc.build"

# --- Modules -------------------------------------------------------------------
# Disable every module, then opt back in to the ones Songo needs.
modules_enabled_by_default = "no"

module_gdscript_enabled = "yes"
module_glslang_enabled = "yes"  # Required by the Vulkan renderer to compile shaders.

# Text/fonts.
module_freetype_enabled = "yes"
# Fallback text server instead of text_server_adv (HarfBuzz + ICU, ~4 MB).
# Trade-off: no BiDi/complex shaping (Arabic, Hebrew, Indic, Thai render
# incorrectly) and word wrap only breaks at spaces/punctuation, so CJK text
# needs AUTOWRAP_WORD_SMART to wrap.
module_text_server_fb_enabled = "yes"
module_msdfgen_enabled = "yes"  # Themes use MSDF fonts.

# Images. PNG is in core. WebP is also the default lossless format for
# imported 2D textures, so it is required even without runtime loading.
module_svg_enabled = "yes"  # Runtime-loaded dicebear SVG artwork.
module_webp_enabled = "yes"  # Background spritesheets, cover art cache (save_webp).
module_jpg_enabled = "yes"  # Cover art.
module_bmp_enabled = "yes"  # Cover art (load_bmp_from_buffer).

# Audio. Music goes through songo_player_harness; UI sound effects are imported MP3s.
module_minimp3_enabled = "yes"

# Networking/misc.
module_mbedtls_enabled = "yes"  # HTTPS (lrclib lyrics, network status check).
module_regex_enabled = "yes"
module_noise_enabled = "yes"  # FastNoiseLite/NoiseTexture2D in the Noted theme.

# Music library: tag reading, directory scans, cover art (GDTagLib class).
module_gdtaglib_enabled = "yes"

# --- Build time ------------------------------------------------------------------
# Single compilation unit build: much faster full rebuilds. Disable if you hit
# odd duplicate-symbol errors, or run low on RAM.
scu_build = "yes"
