#pragma once

// EXR I/O via OpenImageIO, color-correct by construction: pixels are read and
// written as float RGBA, metadata is preserved, and no display transform is
// ever applied here. Files stay in their working space.

#include <OpenColorIO/OpenColorIO.h>

namespace OCIO = OCIO_NAMESPACE;

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace ocioexr {

// {"ok": bool, "error": String, "image": Image (RGBA float), "space": String,
//  "metadata": Dictionary, "channels": PackedStringArray} — "space" from the
//  file metadata when it matches a config colorspace name (case-insensitive),
//  else "". "channels" lists every channel name in file order (layers/AOVs).
//
// `use_channels` (IMG-20) selects the channels to read, by NAME, one per RGBA
// slot in order (R,G,B,A) — fewer than 4 is allowed (a grey layer passes the
// same name three times); an empty array keeps the historical behaviour
// (channels 0..3 in file order). Names come from the caller's layer parser,
// so all layer semantics live in GDScript and stay unit-testable.
godot::Dictionary load(const godot::String &path, const OCIO::ConstConfigRcPtr &config,
		const godot::PackedStringArray &use_channels = godot::PackedStringArray());

// Channel names in file order, header only (no pixels read): cheap enough to
// probe a file for its AOV list (IMG-20). Empty on error.
godot::PackedStringArray channels(const godot::String &path);

// Writes `image` as float EXR tagged `space` (also as "colorspace"/"ocio:space"
// attributes), preserving `metadata` entries. No display transform baked in.
godot::Error save(const godot::String &path, const godot::Ref<godot::Image> &image,
		const godot::String &space, const godot::Dictionary &metadata);

} // namespace ocioexr
