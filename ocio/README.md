# OCIO GDExtension (ocio/)

Runtime [OpenColorIO](https://opencolorio.org) (v2.1+) + [OpenImageIO](https://openimageio.org)
as a Godot 4 GDExtension: exact color transforms, OCIO-generated GPU shaders,
and float EXR I/O — replacing any baked-LUT approximation.

- `OcioTransform.load_config()` loads a real `.ocio` config
  (`OCIO::Config::CreateFromFile`); an empty path loads OCIO's builtin
  `studio-config-v1.0.0_aces-v1.3_ocio-v2.1`. The extension ships no config
  of its own: default = that builtin, or any `.ocio` path the caller loads.
- CPU processors are exact: `config->getProcessor(src, dst)` and a
  `GroupTransform` of `LookTransform` + `DisplayViewTransform` for
  display/view/look, evaluated via `CPUProcessor::apply(PackedImageDesc)`.
- `get_gpu_shader()` (working → display/view) and `get_space_shader()`
  (any space → space, the per-image input transform) return OCIO's
  optimized GPU shaders (`extractGpuShaderInfo`, GLSL 1.3 flavor) with
  `texture3D(`/`texture2D(` renamed to `texture(` so they compile as Godot
  `canvas_item` shaders, plus the LUT texture data OCIO generated.
- `load_exr` / `save_exr` do float RGBA EXR I/O through OIIO with metadata
  preservation. Saved EXRs stay in the working space — no display transform
  is ever baked into pixels.

## Layout

```
ocio/                       C++ source + build files (this directory)
  src/                        ocio_transform.cpp/h, exr_io.cpp/h, register_types.cpp
  ocio.gdextension            descriptor template (macOS deps injected by the build)
  SConstruct                  scons build; installs into ../dist/ocio/<platform>/
  tools/macos-bundle.sh        portable macOS bundling (stage/merge, @rpath rewrite,
                              universal lipo, codesign, deps snippet)
godot-cpp/                  submodule — master (v10), api_version=4.7 → Godot 4.7
dist/ocio/                  build output (gitignored; CI uploads it as an artifact)
  macos/                      universal dylibs + bundled OCIO/OIIO dylibs +
                              ready-to-use ocio.gdextension + dependencies-snippet.txt
  linux64/, win64/            built binaries
.github/workflows/build-macos.yml   macOS universal CI build
```

To consume, copy the CI artifact (or your local `dist/ocio/<platform>/`)
into the target Godot project as `addons/ocio/<platform>/` and
`addons/ocio/ocio.gdextension` (paths in the descriptor are
`res://addons/ocio/...`).

## Build (Linux, native)

Requirements: C++17 toolchain, `scons`, `pkg-config`,
`OpenColorIO >= 2.3` (tested 2.6.0) and `OpenImageIO >= 2.3` (tested 3.2.1;
Ubuntu: `sudo apt install scons build-essential pkg-config libopencolorio-dev
libopenimageio-dev`; linuxbrew: `brew install scons opencolorio openimageio`
and build with `PKG_CONFIG_PATH=$(brew --prefix)/lib/pkgconfig`).

```sh
git submodule update --init
cd ocio
scons target=template_debug        # installs into ../dist/ocio
scons target=template_release
```

godot-cpp is built on demand from the submodule; its master SConstruct
generates bindings for the engine API picked by `api_version` (default
`4.7`, the API this project targets; supported values 4.3–4.8).

Useful arguments: `arch=arm64|x86_64`, `rpath=0` (skip the `-Wl,-rpath`
to the dependency libdir — CI/portable builds), `install=0` (build the
binary without copying into `../dist`), `use_static_cpp=no`.

## Build (macOS, universal)

CI (`.github/workflows/build-macos.yml`) does the full pipeline; locally:

```sh
brew install scons pkg-config opencolorio openimageio        # arm64 (/opt/homebrew)
cd ocio
for target in template_debug template_release; do
  scons platform=macos arch=arm64   target=$target install=0 rpath=0
  PATH="/usr/local/bin:$PATH" scons platform=macos arch=x86_64 target=$target install=0 rpath=0
  tools/macos-bundle.sh stage libgdocio.macos.$target.arm64.dylib   ../build/stage/arm64/$target
  PATH="/usr/local/bin:$PATH" tools/macos-bundle.sh stage libgdocio.macos.$target.x86_64.dylib ../build/stage/x86_64/$target
  tools/macos-bundle.sh merge $target ../build/stage/arm64/$target ../build/stage/x86_64/$target ../dist/ocio/macos
done
```

x86_64 needs the Intel Homebrew under Rosetta (`/usr/local`, see the
workflow) for the dependency slices; Apple clang cross-compiles the code.

The bundling makes the addon **portable**: every Homebrew dependency is
copied next to the library as a universal dylib, all install names are
rewritten to `@rpath/<name>`, and the library carries
`@loader_path` + `@executable_path/../Frameworks` rpaths — so it runs in
the editor (deps preloaded from the addon folder via `[dependencies]`)
and in macOS exports (deps copied to `Contents/Frameworks` by Godot).
`merge` also ad-hoc codesigns everything (required on arm64) and writes
`dist/ocio/macos/ocio.gdextension` (template + generated macOS
`[dependencies]` section) plus `dependencies-snippet.txt`.

## CI notes (GitHub Actions)

`build-macos.yml` (arm64 runner) per push/PR to `main`:

- Installs arm64 deps with native Homebrew, x86_64 deps with Intel
  Homebrew under Rosetta, builds debug+release for both arches, stages,
  lipo-merges, codesigns, verifies (`lipo -archs`, no unrelocated
  `otool -L` refs, standalone `dlopen` smoke test) and uploads
  `dist/ocio/macos/` as the `ocio-macos-universal` artifact.
- Homebrew formula versions can drift between the two prefixes — `merge`
  fails if the staged dependency sets differ across arches.
- Linux: `apt-get install scons build-essential pkg-config
  libopencolorio-dev libopenimageio-dev` + the two scons lines with
  `install=0`. Ubuntu 24.04 ships OCIO 2.3 / OIIO 2.5, so do not expect
  bit-identical transforms vs the 2.6-built binaries — pin/bundle if
  that matters.
- Portable Linux export: the checked-in `.so` keeps a `RUNPATH` to the
  build machine's brew Cellar. For distribution, build OCIO/OIIO with
  `-DBUILD_SHARED_LIBS=OFF` and link the static archives, or bundle the
  shared libs via `[dependencies]` in `ocio.gdextension` like the macOS
  bundle. Check with `ldd libgdocio*.so | grep -E 'OpenColorIO|OpenImageIO'`.

## Config resolution

No config ships inside the app. An empty `load_config()` path uses OCIO's
builtin ACES 1.3 studio config (no files); an existing file path is loaded
from disk — a user-provided `.ocio` (its LUT files must resolve). A non-file
string is treated as a builtin registry name (`get_builtin_configs`).

## Validate

The class is a plain `OcioTransform : RefCounted`; any GDScript can drive it:

```gdscript
var t := OcioTransform.new()
t.load_config("")                      # builtin ACES 1.3 studio config
print(t.get_ocio_version(), t.get_color_spaces())
var img: Image = t.apply_display(src, "sRGB - Display", "ACES 1.0 - SDR Video", "", 0.0)
var shader: Dictionary = t.get_gpu_shader("sRGB - Display", "ACES 1.0 - SDR Video", "", "ocio_display")
```

Independent reference (same builtin config, display, view):

```sh
oiiotool in.exr --colorconfig "ocio://studio-config-v1.0.0_aces-v1.3_ocio-v2.1" \
  --ociodisplay "sRGB - Display" "ACES 1.0 - SDR Video" -d uint8 -o ref.exr
```

Verified on the source project (2026-10-22): greys 0.18/1.0/4.0/0.02 →
91/207/244/11 both ways, and the composed shader (OCIO input-space function
+ exposure + display-view function) matches the CPU path to 0.1/255 through
the ACES 1.3 and 2.1 builtins, Forward+ and Compatibility, for ACEScg and
ACEScc-encoded inputs.

The ACES 1.3 display transform generates **no GPU LUTs** — the shader is
pure arithmetic (matrices + grading curves). The ACES 2.x transform uses 1D
LUT tables (reach/gamut). Godot's shader language needed three rewrites of
OCIO's GLSL (done in `OcioTransform.get_gpu_shader`): the grading-curve
knot search becomes a while loop (Godot requires a for-init declaration),
`matN(...)*v` becomes explicit vector math (no N²-scalar matrix
constructor), and `uniform sampler1D` becomes `sampler2D` with `vec2(x, 0.5)`
coordinates — the 1D data is uploaded as w×1 2D textures, and the LUT
uniforms are keyed by their **sampler** names as declared in the shader.

## Known issues

- Linux `.so` binaries are not relocatable (see CI notes above).
- The bundled macOS dylibs require whatever macOS the Homebrew bottles
  were built for (no explicit deployment target is pinned); rebuild older
  targets locally if you need to support older macOS versions.
