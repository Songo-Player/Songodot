# Build profile for Songodot SBC (arm64) export templates.
#
# Usage:
#   scons profile=sbc_profile.py -j 16
#
# Any option given on the command line overrides the value here, e.g.:
#   scons profile=sbc_profile.py target=template_debug -j 16
#
# Note: -j is a SCons option, not a build variable, so it can't be set here.
# If omitted, Godot's SConstruct defaults to (CPU count - 1) jobs.
#
# Tuned for Songo (../Songo-5): a 2D, UI-only app. Audio/video playback goes
# through the FFmpeg GDExtension, so most engine codecs are unnecessary.

platform = "sbc"
arch = "arm64"
target = "template_release"

use_llvm = "yes"
progress = "yes"

# --- Renderers ---------------------------------------------------------------
vulkan = "yes"
opengl3 = "yes"

# --- Size ----------------------------------------------------------------------
optimize = "size"
debug_symbols = "no"
# lto=full shrinks the binary further but makes linking much slower (and more
# RAM hungry). Try "thin" for a middle ground when making a final release.
lto = "full"

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
# OptionButton, PopupMenu, TextEdit, etc. Setting this to "yes" saves some
# size, since Songo itself doesn't use those controls.
disable_advanced_gui = "no"
# Unregisters unused 2D classes: physics nodes/shapes, 2D lights, particles,
# tilemaps, navigation nodes, skeletons, parallax. With lto=full their code is
# then stripped. The 2D physics server itself can't be removed in 4.3.
# A theme/plugin .pck using any of these classes would fail to load them.
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

# Audio. Music goes through FFmpeg; UI sound effects are imported MP3s.
module_minimp3_enabled = "yes"

# Networking/misc.
module_mbedtls_enabled = "yes"  # HTTPS (lrclib lyrics, network status check).
module_regex_enabled = "yes"
module_noise_enabled = "yes"  # FastNoiseLite/NoiseTexture2D in the Noted theme.

# --- Build time ------------------------------------------------------------------
# Single compilation unit build: much faster full rebuilds. Disable if you hit
# odd duplicate-symbol errors in platform/sbc code, or run low on RAM.
scu_build = "yes"
