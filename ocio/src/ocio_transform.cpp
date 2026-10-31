#include "ocio_transform.h"

#include <godot_cpp/classes/file_access.hpp>

#include <algorithm>
#include <cstring>
#include <functional>
#include <sstream>
#include <regex>
#include <string>
#include <vector>

#include "exr_io.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// Rewrites OCIO's optimized GLSL into Godot-compatible shader language
// (verified against Godot 4.7's parser):
// - texture3D(/texture2D( -> texture(
// - GradingBSplineCurve knot search: OCIO pre-declares `int i` and uses its
//   exit value, but Godot requires the for-init to be a declaration -> an
//   equivalent while loop.
// - matN(f0..fn) * vec: Godot has no N*N-scalar matrix constructor ->
//   explicit vector math (GLSL column-major order preserved).
static std::string regex_replace_cb(const std::string &in, const std::regex &re,
		const std::function<std::string(const std::smatch &)> &cb) {
	std::string out;
	auto last = in.begin();
	for (std::sregex_iterator it(in.begin(), in.end(), re), end; it != end; ++it) {
		out.append(last, in.begin() + it->position());
		out.append(cb(*it));
		last = in.begin() + it->position() + it->length();
	}
	out.append(last, in.end());
	return out;
}

static std::string replace_all_str(std::string in, const std::string &from, const std::string &to) {
	size_t at = 0;
	while ((at = in.find(from, at)) != std::string::npos) {
		in.replace(at, from.size(), to);
		at += to.size();
	}
	return in;
}

// Rewrites `texture(NAME, coord)` into `texture(NAME, vec2(coord, 0.5))`
// for 1D samplers converted to 2D (balanced-paren scan, no regex).
static void wrap_1d_coords(std::string &text, const std::string &name) {
	const std::string head = "texture(" + name + ",";
	size_t at = 0;
	while ((at = text.find(head, at)) != std::string::npos) {
		const size_t open = at + std::string("texture(").size() - 1;
		int depth = 1;
		size_t k = open + 1;
		while (k < text.size() && depth > 0) {
			if (text[k] == '(') {
				++depth;
			} else if (text[k] == ')') {
				--depth;
			}
			++k;
		}
		const size_t comma = text.find(',', open);
		if (comma == std::string::npos || k == 0 || depth != 0) {
			at += head.size();
			continue;
		}
		text.insert(k - 1, ", 0.5)");  // matching ')' of texture(
		text.insert(comma + 1, "vec2(");
		at = comma + 6 + (k - comma);  // past the rewritten call
	}
}

static std::string adapt_shader_for_godot(std::string text) {
	text = replace_all_str(text, "texture3D(", "texture(");
	text = replace_all_str(text, "texture2D(", "texture(");

	// 1D LUT textures (ACES 2.x reach/gamut tables): Godot has no sampler1D;
	// the data is uploaded as a w×1 2D texture, so the type is renamed and the
	// coordinate wrapped in vec2(x, 0.5).
	std::vector<std::string> names_1d;
	static const std::regex s1d_re("uniform sampler1D (\\w+);");
	for (std::sregex_iterator it(text.begin(), text.end(), s1d_re), end; it != end; ++it) {
		names_1d.push_back((*it)[1].str());
	}
	for (const std::string &n : names_1d) {
		text = replace_all_str(text, "uniform sampler1D " + n + ";", "uniform sampler2D " + n + ";");
		wrap_1d_coords(text, n);
	}

	static const std::regex loop_re(
			"int i = 0;[ \\t]*\\n[ \\t]*"
			"for \\(i = 0; i < knotsCnt - 2; \\+\\+i\\)[ \\t]*\\n[ \\t]*\\{[ \\t]*\\n[ \\t]*"
			"if \\(x < ([^)]*)\\)[ \\t]*\\n[ \\t]*\\{[ \\t]*\\n[ \\t]*break;[ \\t]*\\n[ \\t]*\\}[ \\t]*\\n[ \\t]*\\}");
	text = regex_replace_cb(text, loop_re, [](const std::smatch &m) {
		return "int i = 0;\n  while (i < knotsCnt - 2 && !(x < " + m.str(1) + "))\n  {\n    ++i;\n  }";
	});

	static const std::regex mat_re("mat([234])\\(([^)]*)\\) \\* (\\w+)");
	text = regex_replace_cb(text, mat_re, [](const std::smatch &m) {
		const int n = std::stoi(m.str(1));
		std::vector<std::string> vals;
		std::string list = m.str(2), item;
		std::stringstream ss(list);
		while (std::getline(ss, item, ',')) {
			vals.push_back(item);
		}
		if ((int)vals.size() != n * n) {
			return m.str(0); // unexpected shape: leave untouched (parse will report)
		}
		static const char *COMPS[4] = { "x", "y", "z", "w" };
		const std::string v = m.str(3);
		std::string out = "vec" + std::to_string(n) + "(";
		for (int r = 0; r < n; ++r) {
			out += "(";
			for (int c = 0; c < n; ++c) {
				if (c) {
					out += " + ";
				}
				out += vals[(size_t)c * n + r] + " * " + v + "." + COMPS[c];
			}
			out += ")";
			if (r + 1 < n) {
				out += ", ";
			}
		}
		out += ")";
		return out;
	});
	return text;
}


void OcioTransform::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_config", "path"), &OcioTransform::load_config);
	ClassDB::bind_method(D_METHOD("is_loaded"), &OcioTransform::is_loaded);
	ClassDB::bind_method(D_METHOD("get_config_path"), &OcioTransform::get_config_path);
	ClassDB::bind_method(D_METHOD("get_config_description"), &OcioTransform::get_config_description);
	ClassDB::bind_method(D_METHOD("get_ocio_version"), &OcioTransform::get_ocio_version);
	ClassDB::bind_method(D_METHOD("get_builtin_configs"), &OcioTransform::get_builtin_configs);
	ClassDB::bind_method(D_METHOD("get_working_space"), &OcioTransform::get_working_space);
	ClassDB::bind_method(D_METHOD("get_color_spaces"), &OcioTransform::get_color_spaces);
	ClassDB::bind_method(D_METHOD("get_displays"), &OcioTransform::get_displays);
	ClassDB::bind_method(D_METHOD("get_default_display"), &OcioTransform::get_default_display);
	ClassDB::bind_method(D_METHOD("get_views", "display"), &OcioTransform::get_views);
	ClassDB::bind_method(D_METHOD("get_looks"), &OcioTransform::get_looks);
	ClassDB::bind_method(D_METHOD("apply_space", "image", "src_space", "dst_space"), &OcioTransform::apply_space);
	ClassDB::bind_method(D_METHOD("apply_display", "image", "display", "view", "look", "exposure_stops"), &OcioTransform::apply_display);
	ClassDB::bind_method(D_METHOD("get_gpu_shader", "display", "view", "look", "function_name"), &OcioTransform::get_gpu_shader);
	ClassDB::bind_method(D_METHOD("get_space_shader", "src_space", "dst_space", "function_name"), &OcioTransform::get_space_shader);
	ClassDB::bind_method(D_METHOD("load_exr", "path", "channels"), &OcioTransform::load_exr, DEFVAL(PackedStringArray()));
	ClassDB::bind_method(D_METHOD("get_exr_channels", "path"), &OcioTransform::get_exr_channels);
	ClassDB::bind_method(D_METHOD("save_exr", "path", "image", "space", "metadata"), &OcioTransform::save_exr);
}

String OcioTransform::get_ocio_version() const {
	return String(OCIO::GetVersion());
}

Dictionary OcioTransform::get_builtin_configs() const {
	Dictionary out;
	try {
		const OCIO::BuiltinConfigRegistry &reg = OCIO::BuiltinConfigRegistry::Get();
		for (size_t i = 0, n = reg.getNumBuiltinConfigs(); i < n; ++i) {
			const char *name = reg.getBuiltinConfigName(i);
			const char *ui = reg.getBuiltinConfigUIName(i);
			if (name != nullptr) {
				out[String(name)] = String(ui ? ui : name);
			}
		}
	} catch (const OCIO::Exception &e) {
		ERR_PRINT("OCIO builtin registry failed: " + String(e.what()));
	}
	return out;
}

Error OcioTransform::load_config(const String &path) {
	_config.reset();
	_config_path = "";
	_config_description = "";
	try {
		if (path.is_empty()) {
			// OCIO 2.1+ builtin: same config tools/bake_aces_luts.py uses,
			// so baked-LUT fallback and runtime OCIO agree by default.
			_config = OCIO::Config::CreateFromBuiltinConfig("studio-config-v1.0.0_aces-v1.3_ocio-v2.1");
			_config_path = "builtin:studio-config-v1.0.0_aces-v1.3_ocio-v2.1";
		} else if (godot::FileAccess::file_exists(path)) {
			_config = OCIO::Config::CreateFromFile(path.utf8().get_data());
			_config_path = path;
		} else {
			// Not a file: a builtin config name (e.g. an ACES 2.x one).
			const std::string name(path.utf8().get_data());
			_config = OCIO::Config::CreateFromBuiltinConfig(name.c_str());
			_config_path = String("builtin:") + path;
		}
		_config_description = String(_config->getDescription() ? _config->getDescription() : "");
	} catch (const OCIO::Exception &e) {
		ERR_PRINT("OCIO config load failed: " + String(e.what()));
		return Error::ERR_CANT_OPEN;
	} catch (const std::exception &e) {
		ERR_PRINT("OCIO config load failed: " + String(e.what()));
		return Error::ERR_CANT_OPEN;
	}
	return Error::OK;
}

bool OcioTransform::is_loaded() const {
	return _config != nullptr;
}

String OcioTransform::get_config_path() const {
	return _config_path;
}

String OcioTransform::get_config_description() const {
	return _config_description;
}

String OcioTransform::get_working_space() const {
	if (_config == nullptr) {
		return "";
	}
	// The app works in ACEScg: item shaders convert inputs to ACEScg
	// (ColorSpaces.TO_ACESCG) and saved EXRs stay in it. Only fall back to
	// the config's scene_linear role if it has no ACEScg space.
	try {
		OCIO::ConstColorSpaceRcPtr cg = _config->getColorSpace("ACEScg");
		if (cg) {
			return "ACEScg";
		}
	} catch (const OCIO::Exception &) {
	}
	try {
		OCIO::ConstColorSpaceRcPtr cs = _config->getColorSpace(OCIO::ROLE_SCENE_LINEAR);
		if (cs) {
			return String(cs->getName());
		}
	} catch (const OCIO::Exception &) {
	}
	return "";
}

PackedStringArray OcioTransform::get_color_spaces() const {
	PackedStringArray out;
	if (_config == nullptr) {
		return out;
	}
	for (int i = 0, n = _config->getNumColorSpaces(); i < n; ++i) {
		const char *name = _config->getColorSpaceNameByIndex(i);
		if (name) {
			out.push_back(String(name));
		}
	}
	return out;
}

PackedStringArray OcioTransform::get_displays() const {
	PackedStringArray out;
	if (_config == nullptr) {
		return out;
	}
	for (int i = 0, n = _config->getNumDisplays(); i < n; ++i) {
		const char *name = _config->getDisplay(i);
		if (name) {
			out.push_back(String(name));
		}
	}
	return out;
}

String OcioTransform::get_default_display() const {
	if (_config == nullptr) {
		return "";
	}
	const char *d = _config->getDefaultDisplay();
	return String(d ? d : "");
}

PackedStringArray OcioTransform::get_views(const String &display) const {
	PackedStringArray out;
	if (_config == nullptr) {
		return out;
	}
	const std::string d(display.utf8().get_data());
	for (int i = 0, n = _config->getNumViews(d.c_str()); i < n; ++i) {
		const char *name = _config->getView(d.c_str(), i);
		if (name) {
			out.push_back(String(name));
		}
	}
	return out;
}

PackedStringArray OcioTransform::get_looks() const {
	PackedStringArray out;
	if (_config == nullptr) {
		return out;
	}
	for (int i = 0, n = _config->getNumLooks(); i < n; ++i) {
		const char *name = _config->getLookNameByIndex(i);
		if (name) {
			out.push_back(String(name));
		}
	}
	return out;
}

OCIO::ConstProcessorRcPtr OcioTransform::_display_processor(const String &display, const String &view, const String &look) const {
	if (_config == nullptr) {
		return OCIO::ConstProcessorRcPtr();
	}
	const std::string working(get_working_space().utf8().get_data());
	const std::string d(display.utf8().get_data());
	const std::string v(view.utf8().get_data());
	const std::string l(look.utf8().get_data());
	try {
		OCIO::GroupTransformRcPtr group = OCIO::GroupTransform::Create();
		if (!l.empty() && l != "default") {
			OCIO::LookTransformRcPtr lt = OCIO::LookTransform::Create();
			lt->setLooks(l.c_str());
			lt->setSrc(working.c_str());
			lt->setDst(working.c_str());
			group->appendTransform(lt);
		}
		OCIO::DisplayViewTransformRcPtr dvt = OCIO::DisplayViewTransform::Create();
		dvt->setSrc(working.c_str());
		dvt->setDisplay(d.c_str());
		dvt->setView(v.c_str());
		group->appendTransform(dvt);
		return _config->getProcessor(group, OCIO::TRANSFORM_DIR_FORWARD);
	} catch (const OCIO::Exception &e) {
		ERR_PRINT("OCIO display processor failed: " + String(e.what()));
		return OCIO::ConstProcessorRcPtr();
	}
}

Error OcioTransform::_apply_to_image(const Ref<Image> &image, const OCIO::ConstCPUProcessorRcPtr &cpu, float gain) {
	if (image.is_null() || cpu == nullptr) {
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
	PackedByteArray data = img->get_data();
	const int64_t count = (int64_t)w * h * 4;
	if (data.size() != count * (int64_t)sizeof(float)) {
		return Error::ERR_INVALID_DATA;
	}
	float *px = reinterpret_cast<float *>(data.ptrw());
	if (gain != 1.0f) {
		for (int64_t i = 0; i < count; ++i) {
			px[i] *= gain;
		}
	}
	// Exact CPU evaluation, no LUT quantization or interpolation.
	OCIO::PackedImageDesc desc(px, w, h, 4);
	cpu->apply(desc);
	img->set_data(w, h, false, Image::FORMAT_RGBAF, data);
	return Error::OK;
}

Error OcioTransform::apply_space(const Ref<Image> &image, const String &src_space, const String &dst_space) {
	if (_config == nullptr || image.is_null()) {
		return Error::ERR_UNCONFIGURED;
	}
	try {
		OCIO::ConstProcessorRcPtr proc = _config->getProcessor(
				src_space.utf8().get_data(), dst_space.utf8().get_data());
		if (proc == nullptr) {
			return Error::ERR_DOES_NOT_EXIST;
		}
		return _apply_to_image(image, proc->getDefaultCPUProcessor(), 1.0f);
	} catch (const OCIO::Exception &e) {
		ERR_PRINT("OCIO apply_space failed: " + String(e.what()));
		return Error::ERR_INVALID_PARAMETER;
	}
}

Error OcioTransform::apply_display(const Ref<Image> &image, const String &display, const String &view, const String &look, double exposure_stops) {
	OCIO::ConstProcessorRcPtr proc = _display_processor(display, view, look);
	if (proc == nullptr) {
		return Error::ERR_DOES_NOT_EXIST;
	}
	OCIO::ConstCPUProcessorRcPtr cpu = proc->getDefaultCPUProcessor();
	if (cpu == nullptr) {
		return Error::ERR_DOES_NOT_EXIST;
	}
	return _apply_to_image(image, cpu, std::pow(2.0f, (float)exposure_stops));
}

Dictionary OcioTransform::_extract_gpu_shader(const OCIO::ConstProcessorRcPtr &proc, const String &function_name) {
	Dictionary out;
	out["shader"] = "";
	out["luts"] = Dictionary();
	out["error"] = "";
	if (proc == nullptr) {
		out["error"] = "cannot build processor";
		return out;
	}
	std::string fn = function_name.is_empty() ? "ocio_display_map" : std::string(function_name.utf8().get_data());
	try {
		OCIO::GpuShaderDescRcPtr desc = OCIO::GpuShaderDesc::CreateShaderDesc();
		// GLSL 1.3 flavor is closest to Godot's shader language; the two
		// texture-function renames below make it compile as-is.
		desc->setLanguage(OCIO::GPU_LANGUAGE_GLSL_1_3);
		desc->setFunctionName(fn.c_str());
		proc->getDefaultGPUProcessor()->extractGpuShaderInfo(desc);
		std::string text = adapt_shader_for_godot(desc->getShaderText());
		Dictionary luts;
		// 3D textures: RGBA floats, edge length per side.
		for (unsigned i = 0, n = desc->getNum3DTextures(); i < n; ++i) {
			const char *tex_name = nullptr;
			const char *sampler_name = nullptr;
			unsigned edge = 0;
			OCIO::Interpolation interp = OCIO::INTERP_DEFAULT;
			desc->get3DTexture(i, tex_name, sampler_name, edge, interp);
			const float *values = nullptr;
			desc->get3DTextureValues(i, values);
			if (tex_name == nullptr || values == nullptr || edge == 0) {
				continue;
			}
			Dictionary lut;
			lut["type"] = "3d";
			lut["channels"] = 4;
			lut["width"] = (int64_t)edge;
			lut["height"] = (int64_t)edge;
			lut["depth"] = (int64_t)edge;
			PackedFloat32Array data;
			data.resize((int64_t)edge * edge * edge * 4);
			std::memcpy(data.ptrw(), values, (size_t)edge * edge * edge * 4 * sizeof(float));
			lut["data"] = data;
			luts[String(sampler_name != nullptr ? sampler_name : tex_name)] = lut;
		}
		// 1D/2D textures: RED (1) or RGB (3) floats per texel.
		for (unsigned i = 0, n = desc->getNumTextures(); i < n; ++i) {
			const char *tex_name = nullptr;
			const char *sampler_name = nullptr;
			unsigned w = 0;
			unsigned h = 0;
			OCIO::GpuShaderCreator::TextureType ttype = OCIO::GpuShaderCreator::TEXTURE_RGB_CHANNEL;
			OCIO::GpuShaderCreator::TextureDimensions dims = OCIO::GpuShaderCreator::TEXTURE_1D;
			OCIO::Interpolation interp = OCIO::INTERP_DEFAULT;
			desc->getTexture(i, tex_name, sampler_name, w, h, ttype, dims, interp);
			const float *values = nullptr;
			desc->getTextureValues(i, values);
			if (tex_name == nullptr || values == nullptr || w == 0) {
				continue;
			}
			const int comps = ttype == OCIO::GpuShaderCreator::TEXTURE_RED_CHANNEL ? 1 : 3;
			Dictionary lut;
			lut["type"] = dims == OCIO::GpuShaderCreator::TEXTURE_2D ? "2d" : "1d";
			lut["channels"] = comps;
			lut["width"] = (int64_t)w;
			lut["height"] = (int64_t)h;
			lut["depth"] = 1;
			PackedFloat32Array data;
			data.resize((int64_t)w * h * comps);
			std::memcpy(data.ptrw(), values, (size_t)w * h * comps * sizeof(float));
			lut["data"] = data;
			luts[String(sampler_name != nullptr ? sampler_name : tex_name)] = lut;
		}
		out["shader"] = String(text.c_str());
		out["luts"] = luts;
	} catch (const OCIO::Exception &e) {
		out["error"] = String(e.what());
	}
	return out;
}

Dictionary OcioTransform::get_gpu_shader(const String &display, const String &view, const String &look, const String &function_name) {
	return _extract_gpu_shader(_display_processor(display, view, look), function_name);
}

Dictionary OcioTransform::get_space_shader(const String &src_space, const String &dst_space, const String &function_name) {
	Dictionary none;
	none["shader"] = "";
	none["luts"] = Dictionary();
	if (_config == nullptr) {
		none["error"] = "no OCIO config loaded";
		return none;
	}
	OCIO::ConstProcessorRcPtr proc;
	try {
		proc = _config->getProcessor(src_space.utf8().get_data(), dst_space.utf8().get_data());
	} catch (const OCIO::Exception &e) {
		none["error"] = String(e.what());
		return none;
	}
	if (proc == nullptr) {
		none["error"] = "cannot build " + src_space + " -> " + dst_space + " processor";
		return none;
	}
	return _extract_gpu_shader(proc, function_name);
}

Dictionary OcioTransform::load_exr(const String &path, const PackedStringArray &use_channels) {
	Dictionary out;
	out["ok"] = false;
	out["error"] = "";
	out["image"] = Variant();
	out["space"] = "";
	out["metadata"] = Dictionary();
	out["channels"] = PackedStringArray();
	if (_config == nullptr) {
		out["error"] = "no OCIO config loaded";
		return out;
	}
	Dictionary r = ocioexr::load(path, _config, use_channels);
	out["ok"] = r.get("ok", false);
	out["error"] = r.get("error", "");
	out["image"] = r.get("image", Variant());
	out["space"] = r.get("space", "");
	out["metadata"] = r.get("metadata", Dictionary());
	out["channels"] = r.get("channels", PackedStringArray());
	return out;
}

PackedStringArray OcioTransform::get_exr_channels(const String &path) {
	return ocioexr::channels(path);
}

Error OcioTransform::save_exr(const String &path, const Ref<Image> &image, const String &space, const Dictionary &metadata) {
	return ocioexr::save(path, image, space, metadata);
}
