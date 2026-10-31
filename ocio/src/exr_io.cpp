#include "exr_io.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace ocioexr {

// --- helpers -----------------------------------------------------------------

static Variant param_to_variant(const OIIO::ParamValue &pv) {
	const OIIO::TypeDesc &t = pv.type();
	const void *d = pv.data();
	switch (t.basetype) {
		case OIIO::TypeDesc::INT8:
		case OIIO::TypeDesc::UINT8:
		case OIIO::TypeDesc::INT:
		case OIIO::TypeDesc::UINT:
			if (t.arraylen == 0) {
				return Variant(static_cast<int64_t>(*(const int *)d));
			}
			break;
		case OIIO::TypeDesc::HALF:
		case OIIO::TypeDesc::FLOAT:
		case OIIO::TypeDesc::DOUBLE:
			if (t.arraylen == 0) {
				return Variant(*(const float *)d);
			}
			break;
		case OIIO::TypeDesc::STRING:
			if (t.arraylen == 0) {
				// ParamValue stores strings as ustring objects.
				const OIIO::ustring s = *(const OIIO::ustring *)d;
				return Variant(String(s.c_str()));
			}
			break;
		default:
			break;
	}
	return Variant(); // unsupported shapes stay untouched (never guessed)
}

static void variant_to_attribute(OIIO::ImageSpec &spec, const String &key, const Variant &value) {
	const std::string k(key.utf8().get_data());
	switch (value.get_type()) {
		case Variant::INT:
			spec.attribute(k, (int)(int64_t)value);
			break;
		case Variant::FLOAT:
			spec.attribute(k, (float)value);
			break;
		case Variant::STRING:
			spec.attribute(k, String(value).utf8().get_data());
			break;
		default:
			break; // complex attrs stay on the GDScript side only
	}
}

// Case-insensitive match of an EXR colorspace attribute against the config.
static String match_space(const OIIO::ImageSpec &spec, const OCIO::ConstConfigRcPtr &config) {
	static const char *KEYS[] = { "ocio:space", "colorspace", nullptr };
	for (int k = 0; KEYS[k] != nullptr; ++k) {
		const std::string v = spec.get_string_attribute(KEYS[k]);
		if (v.empty()) {
			continue;
		}
		std::string lower = v;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
		for (int i = 0, n = config->getNumColorSpaces(); i < n; ++i) {
			const char *name = config->getColorSpaceNameByIndex(i);
			if (name == nullptr) {
				continue;
			}
			std::string cn = name;
			std::transform(cn.begin(), cn.end(), cn.begin(), [](unsigned char c) { return std::tolower(c); });
			if (cn == lower) {
				return String(name);
			}
		}
	}
	return "";
}

// --- load --------------------------------------------------------------------

Dictionary load(const String &path, const OCIO::ConstConfigRcPtr &config, const PackedStringArray &use_channels) {
	Dictionary out;
	out["ok"] = false;
	out["error"] = "";
	out["image"] = Variant();
	out["space"] = "";
	out["metadata"] = Dictionary();
	out["channels"] = PackedStringArray();

	const std::string p(path.utf8().get_data());
	OIIO::ImageBuf buf(p);
	if (!buf.initialized()) {
		out["error"] = "EXR open failed: " + String(buf.geterror().c_str());
		return out;
	}
	const OIIO::ImageSpec &spec = buf.spec();
	const int w = spec.width;
	const int h = spec.height;
	if (w <= 0 || h <= 0) {
		out["error"] = "EXR has no pixels";
		return out;
	}

	// Channel names in file order: the GDScript layer parser (ExrLayers) turns
	// them into AOV entries and picks the names read below (IMG-20).
	PackedStringArray names;
	for (int c = 0; c < spec.nchannels; ++c) {
		names.push_back(String(spec.channel_name(c).c_str()));
	}
	out["channels"] = names;

	// Always deliver RGBA float; channels missing from the file are 0.
	std::vector<float> px((size_t)w * h * 4, 0.0f);
	bool ok = true;
	bool alpha_written = false;
	if (use_channels.size() > 0) {
		// Named selection (IMG-20): read only the requested channels, each by
		// name into its slot. Missing names stay 0 (never guessed).
		const int slots = std::min(use_channels.size(), (int64_t)4);
		std::vector<float> tmp((size_t)w * h, 0.0f);
		for (int64_t slot = 0; slot < slots; ++slot) {
			const std::string name(use_channels[slot].utf8().get_data());
			const int idx = spec.channelindex(name);
			if (idx < 0) {
				continue;
			}
			std::fill(tmp.begin(), tmp.end(), 0.0f);
			ok = ok && buf.get_pixels(OIIO::ROI(spec.x, spec.x + w, spec.y, spec.y + h, spec.z, spec.z + 1, idx, idx + 1),
					OIIO::span<float>(tmp.data(), tmp.size()));
			for (int64_t i = 0, n = (int64_t)w * h; i < n; ++i) {
				px[(size_t)i * 4 + (size_t)slot] = tmp[(size_t)i];
			}
			if (slot == 3) {
				alpha_written = true;
			}
		}
	} else {
		const int file_ch = std::min(spec.nchannels, 4);
		if (file_ch == 4) {
			ok = buf.get_pixels(OIIO::ROI(spec.x, spec.x + w, spec.y, spec.y + h, spec.z, spec.z + 1, 0, 4),
					OIIO::span<float>(px.data(), px.size()));
		} else {
			std::vector<float> tmp((size_t)w * h * file_ch, 0.0f);
			ok = buf.get_pixels(OIIO::ROI(spec.x, spec.x + w, spec.y, spec.y + h, spec.z, spec.z + 1, 0, file_ch),
					OIIO::span<float>(tmp.data(), tmp.size()));
			if (ok) {
				for (int64_t i = 0, n = (int64_t)w * h; i < n; ++i) {
					std::memcpy(&px[(size_t)i * 4], &tmp[(size_t)i * file_ch], (size_t)file_ch * sizeof(float));
				}
			}
		}
		// No alpha channel in the file: opaque, not black.
		alpha_written = spec.alpha_channel >= 0;
	}
	if (!ok) {
		out["error"] = "EXR read failed: " + String(buf.geterror().c_str());
		return out;
	}
	if (!alpha_written) {
		for (int64_t i = 0, n = (int64_t)w * h; i < n; ++i) {
			px[(size_t)i * 4 + 3] = 1.0f;
		}
	}

	PackedByteArray data;
	data.resize((int64_t)w * h * 4 * (int64_t)sizeof(float));
	std::memcpy(data.ptrw(), px.data(), px.size() * sizeof(float));
	Ref<Image> img = Image::create_from_data(w, h, false, Image::FORMAT_RGBAF, data);
	out["image"] = img;
	out["space"] = match_space(spec, config);

	Dictionary meta;
	for (const auto &pv : spec.extra_attribs) {
		const Variant v = param_to_variant(pv);
		if (v.get_type() != Variant::NIL) {
			meta[String(pv.name().c_str())] = v;
		}
	}
	out["metadata"] = meta;
	out["ok"] = true;
	return out;
}

// --- channels ----------------------------------------------------------------

PackedStringArray channels(const String &path) {
	PackedStringArray out;
	const std::string p(path.utf8().get_data());
	auto in = OIIO::ImageInput::open(p);
	if (!in) {
		return out;
	}
	const OIIO::ImageSpec &spec = in->spec();
	for (int c = 0; c < spec.nchannels; ++c) {
		out.push_back(String(spec.channel_name(c).c_str()));
	}
	in->close();
	return out;
}

// --- save --------------------------------------------------------------------

Error save(const String &path, const Ref<Image> &image, const String &space, const Dictionary &metadata) {
	if (image.is_null()) {
		return Error::ERR_INVALID_PARAMETER;
	}
	Ref<Image> img = image;
	if (img->get_format() != Image::FORMAT_RGBAF) {
		img->convert(Image::FORMAT_RGBAF);  // godot-cpp convert is void
	}
	const int w = img->get_width();
	const int h = img->get_height();
	if (w <= 0 || h <= 0) {
		return Error::ERR_INVALID_PARAMETER;
	}
	const PackedByteArray data = img->get_data();

	const std::string p(path.utf8().get_data());
	std::unique_ptr<OIIO::ImageOutput> outp = OIIO::ImageOutput::create(p);
	if (!outp) {
		return Error::ERR_UNAVAILABLE;
	}
	OIIO::ImageSpec spec(w, h, 4, OIIO::TypeDesc::FLOAT);
	spec.channelnames = { "R", "G", "B", "A" };
	spec.alpha_channel = 3;
	spec.attribute("compression", "zips"); // lossless, scanline-friendly
	spec.attribute("openexr:pixelType", "float"); // never silently downcast to half
	const String sp = space.is_empty() ? "raw" : space;
	spec.attribute("colorspace", sp.utf8().get_data());
	spec.attribute("ocio:space", sp.utf8().get_data());
	Array keys = metadata.keys();
	for (int i = 0; i < keys.size(); ++i) {
		variant_to_attribute(spec, keys[i], metadata[keys[i]]);
	}
	if (!outp->open(p, spec)) {
		return Error::ERR_CANT_OPEN;
	}
	if (!outp->write_image(OIIO::TypeDesc::FLOAT, data.ptr())) {
		outp->close();
		return Error::ERR_FILE_CANT_WRITE;
	}
	if (!outp->close()) {
		return Error::ERR_FILE_CANT_WRITE;
	}
	return Error::OK;
}

} // namespace ocioexr
