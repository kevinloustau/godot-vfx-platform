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
  ocio.gdextension            descriptor template ([dependencies] filled by the build)
  SConstruct                  scons build; installs into ../dist/ocio/<platform>/
  tools/macos-bundle.sh        portable macOS bundling (stage/merge, @rpath rewrite,
                              universal lipo, codesign, deps snippet)
  tools/linux-bundle.sh        portable Linux bundling (full closure, RUNPATH=$ORIGIN)
  tools/compose-gdextension.sh multi-platform descriptor from per-platform snippets
dist/ocio/                  build output (gitignored; CI uploads it as an artifact)
  macos/                      universal dylibs + bundled OCIO/OIIO dylibs +
                              ready-to-use ocio.gdextension + dependencies-snippet.txt
  linux64/                    extension + full closure of .so files (RUNPATH=$ORIGIN)
  win64/                      cross-built DLLs
.github/workflows/build-macos.yml   macOS universal CI build
.github/workflows/build-linux.yml   Linux portable bundle CI build
```

To consume, copy the CI artifact (or your local `dist/ocio/<platform>/`)
into the target Godot project as `addons/ocio/<platform>/`. The descriptor
must list every platform you ship, so compose it from the per-platform
snippets:

```sh
ocio/tools/compose-gdextension.sh <project>/addons/ocio/ocio.gdextension \
  macos:dist/ocio/macos linux:dist/ocio/linux64
```

(the `[libraries]` entries and the Windows DLL list already live in the
template; `compose` only fills in the macOS/Linux `[dependencies]`
sections. Paths in the descriptor are `res://addons/ocio/...`.)

## Build (Linux, native)

Requirements: C++17 toolchain, `scons`, `pkg-config`, `patchelf`, and
`OpenColorIO >= 2.3` (tested 2.6.0) / `OpenImageIO >= 2.3` (tested 3.2.1).
Ubuntu: `sudo apt install scons build-essential pkg-config patchelf
libopencolorio-dev libopenimageio-dev`; linuxbrew: `brew install scons
patchelf opencolorio openimageio` + `PKG_CONFIG_PATH=$(brew --prefix)/lib/pkgconfig`.

```sh
git submodule update --init
cd ocio
scons target=template_debug   rpath=0    # rpath=0: no build paths in RUNPATH
scons target=template_release rpath=0
tools/linux-bundle.sh stage libgdocio.linux.template_release.x86_64.so \
  ../dist/ocio/linux64 "$(brew --prefix)/lib"
tools/linux-bundle.sh verify libgdocio.linux.template_release.x86_64.so \
  ../dist/ocio/linux64
```

`linux-bundle.sh` copies the extension's **full transitive closure** of
shared libraries next to it and sets `RUNPATH=$ORIGIN` on every file, so
the folder is portable: it runs on a machine with none of the build
dependencies installed. System libraries (glibc, libstdc++, libgomp, GL
drivers) are never bundled. `verify` fails if any non-system dependency
resolves outside the bundle, and prints the number of files.

Without the bundling step the `.so` keeps absolute build-machine paths
(`/home/linuxbrew/Cellar/opencolorio/2.6.0/lib`) and only loads on the
machine that built it — `stage` is what makes an addon distributable.

godot-cpp is built on demand from the submodule; its master SConstruct
generates bindings for the engine API picked by `api_version` (default
`4.7`, the API this project targets; supported values 4.3–4.8).

Useful arguments: `arch=arm64|x86_64`, `rpath=0` (no `-Wl,-rpath` to the
dependency libdir), `install=0` (build without copying into `../dist`),
`use_static_cpp=no`.

## Build (macOS, universal)

CI (`.github/workflows/build-macos.yml`) does the full pipeline; locally:

```sh
brew install scons pkg-config                       # build tools
# matching OpenColorIO/OpenImageIO for both arches — conda-forge
# (Homebrew no longer builds x86_64 macOS packages)
CONDA_SUBDIR=osx-arm64 micromamba create -y -p ./env-arm64 \
  -c conda-forge libopencolorio=2.6.0 libopenimageio=3.2.1.2
CONDA_SUBDIR=osx-64    micromamba create -y -p ./env-x86_64 \
  -c conda-forge libopencolorio=2.6.0 libopenimageio=3.2.1.2
cd ocio
for target in template_debug template_release; do
  PKG_CONFIG_PATH=../env-arm64/lib/pkgconfig \
    scons platform=macos arch=arm64 target=$target install=0 rpath=0
  PKG_CONFIG_PATH=../env-x86_64/lib/pkgconfig \
    scons platform=macos arch=x86_64 target=$target install=0 rpath=0
  tools/macos-bundle.sh stage libgdocio.macos.$target.arm64.dylib \
    ../env-arm64/lib ../build/stage/arm64/$target
  tools/macos-bundle.sh stage libgdocio.macos.$target.x86_64.dylib \
    ../env-x86_64/lib ../build/stage/x86_64/$target
  tools/macos-bundle.sh merge $target \
    ../build/stage/arm64/$target ../build/stage/x86_64/$target ../dist/ocio/macos
done
```

Apple clang cross-compiles the x86_64 slices with `-arch x86_64`; the
osx-64 conda packages are only linked against, never executed — no Rosetta
needed. The two envs must hold the **same package versions** (`merge` fails
if the staged dependency sets differ across arches).

The bundling makes the addon **portable**: every dependency is copied next
to the library as a universal dylib, absolute install names are rewritten
to `@rpath/<name>`, and the library carries `@loader_path` +
`@executable_path/../Frameworks` rpaths — so it runs in the editor (deps
preloaded from the addon folder via `[dependencies]`) and in macOS exports
(deps copied to `Contents/Frameworks` by Godot). `merge` also ad-hoc
codesigns everything (required on arm64) and writes
`dist/ocio/macos/ocio.gdextension` (template + generated macOS
`[dependencies]` section) plus `dependencies-snippet.txt`.

## CI notes (GitHub Actions)

Two workflows run per push/PR to `main`.

`build-linux.yml` (ubuntu-latest): installs OCIO/OIIO from conda-forge
(linux-64, same versions as macOS), builds debug+release with `rpath=0`,
bundles with `linux-bundle.sh`, then **moves the conda env away** and
re-verifies + `dlopen`s the bundle, proving nothing points at build-time
paths. Uploads `dist/ocio/linux64/` as the `ocio-linux64` artifact.

`build-macos.yml` (arm64 runner):

- Installs OpenColorIO/OpenImageIO from conda-forge (micromamba) for both
  `osx-arm64` and `osx-64` (pinned, matching versions), builds debug+
  release for both arches, stages, lipo-merges, codesigns, verifies
  (`lipo -archs`, no unrelocated `otool -L` refs, `dlopen` smoke test with
  the dependencies preloaded like the editor does) and uploads
  `dist/ocio/macos/` as the `ocio-macos-universal` artifact.
- Homebrew is only used for the build tools (`scons`, `pkg-config`); it no
  longer installs/builds x86_64 macOS packages, which is why the dependency
  slices come from conda-forge instead.

Both platforms pin the same dependency versions, so CPU transforms agree
across platforms (`OCIO_VERSION` / `OIIO_VERSION` in the workflow `env:`).

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

- The Linux bundle ships ~57 shared libraries (~107 MB) because conda-forge's
  OIIO pulls ffmpeg/libheif/etc. Same for macOS (~306 MB, 92 dylibs).
- Linux bundles glibc-compatible builds but not glibc itself: an exported
  game still needs a glibc at least as new as the CI runner's.
- The bundled macOS dylibs require whatever macOS the conda-forge packages
  were built for (no explicit deployment target is pinned); rebuild older
  targets locally if you need to support older macOS versions.
