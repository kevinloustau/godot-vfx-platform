#pragma once

// OcioTransform — runtime OpenColorIO / OpenImageIO bridge for a color-managed
// Godot viewport. Loads a real .ocio config,
// builds exact CPU processors (space -> space, display/view/look), emits the
// OCIO-optimized GPU shader text for the viewport display pass, and does
// float EXR I/O through OpenImageIO. Saved EXRs always stay in the working
// space (scene-linear, unclamped); the display transform is viewport-only.

#include <OpenColorIO/OpenColorIO.h>

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace OCIO = OCIO_NAMESPACE;

namespace godot {

class OcioTransform : public RefCounted {
	GDCLASS(OcioTransform, RefCounted)

private:
	OCIO::ConstConfigRcPtr _config;
	String _config_path;
	String _config_description;

	// DisplayViewTransform (+ optional Look, applied first in scene-linear)
	// for the config's scene_linear working space. Empty processor on error.
	OCIO::ConstProcessorRcPtr _display_processor(const String &display, const String &view, const String &look) const;
	// In-place RGBA float apply; `gain` (2^stops) is multiplied in first.
	static Error _apply_to_image(const Ref<Image> &image, const OCIO::ConstCPUProcessorRcPtr &cpu, float gain);
	// Godot-adapted GLSL + LUTs of any GPU processor (shared by the
	// display-view and the space-to-space shaders).
	static Dictionary _extract_gpu_shader(const OCIO::ConstProcessorRcPtr &proc, const String &function_name);

protected:
	static void _bind_methods();

public:
	// "" loads OCIO's builtin ACES 1.3 studio config (no files needed).
	Error load_config(const String &path);
	bool is_loaded() const;
	String get_config_path() const;
	String get_config_description() const;
	String get_ocio_version() const;

	// OCIO's embedded configs: {registry name: UI name} (e.g. ACES 2.x).
	Dictionary get_builtin_configs() const;

	// scene_linear role of the config (ACEScg in the ACES configs).
	String get_working_space() const;
	PackedStringArray get_color_spaces() const;
	PackedStringArray get_displays() const;
	String get_default_display() const;
	PackedStringArray get_views(const String &display) const;
	PackedStringArray get_looks() const;

	// Exact CPU transforms, in place on the image (converted to RGBA float).
	Error apply_space(const Ref<Image> &image, const String &src_space, const String &dst_space);
	Error apply_display(const Ref<Image> &image, const String &display, const String &view, const String &look, double exposure_stops);

	// Godot-adapted GLSL (function `function_name`) + LUT textures for the
	// display pass: {"shader": String, "luts": {name: {type, size, data}},
	// "error": String} — "shader" empty on failure.
	Dictionary get_gpu_shader(const String &display, const String &view, const String &look, const String &function_name);

	// Same for any space -> space transform (e.g. the per-image input space
	// to the working space, inlined before the display function).
	Dictionary get_space_shader(const String &src_space, const String &dst_space, const String &function_name);

	// {"ok": bool, "error": String, "image": Image (RGBA float),
	//  "space": String ("" = unknown), "metadata": Dictionary,
	//  "channels": PackedStringArray} — `use_channels` picks the channels to
	//  read by name, one per RGBA slot (empty = first 4 in file order); IMG-20.
	Dictionary load_exr(const String &path, const PackedStringArray &use_channels = PackedStringArray());
	// Channel names of an EXR in file order (header only, IMG-20).
	PackedStringArray get_exr_channels(const String &path);
	// Writes float EXR tagged `space`; never bakes in a display transform.
	Error save_exr(const String &path, const Ref<Image> &image, const String &space, const Dictionary &metadata);
};

} // namespace godot
