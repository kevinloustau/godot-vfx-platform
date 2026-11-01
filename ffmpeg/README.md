# FFmpeg GDExtension (ffmpeg/)

[EIRTeam.FFmpeg](https://github.com/EIRTeam/EIRTeam.FFmpeg) (MIT) —
FFmpeg-based video decoding for Godot 4 (`FFmpegVideoStream`,
`VideoStreamFFMpegLoader`, `FFmpegStreamInfo`, `FFmpegFrame`) — built by
this repo as a portable addon against the **FFmpeg 6.1 LGPL** shared
libraries, the same sonames the upstream release ships (avcodec 60).

The C++ sources are the `ffmpeg-src/` submodule pinned to the upstream
release tag that matches the binaries already used in the ideorama project
(`84ea333`, "Fix YUV->RGB compute pipeline never executing on Godot 4.7").
This folder only holds the build glue; the submodule stays unpatched.

## What this build changes vs upstream

- godot-cpp master with `api_version=4.7` (Godot 4.7 bindings), shared with
  the rest of this repo. Upstream pins a godot-cpp 4.2 submodule.
- `compat/compat_force.hpp` is force-included before every translation
  unit: godot-cpp master moved `Mutex` into namespace `godot::CoreBind`,
  upstream code says `godot::Mutex`. `compat/godot_cpp/core/mutex_lock.hpp`
  re-provides the RAII helper godot-cpp dropped after 4.2.
- Dependencies come from conda-forge (`ffmpeg=6.1.2=lgpl*`, all platforms)
  instead of upstream's prebuilt kits, so all three platforms run the same
  FFmpeg version, LGPL-2.1 (no GPL components).

## Layout

```
ffmpeg-src/                submodule — EIRTeam/EIRTeam.FFmpeg @ 84ea333 (sources)
ffmpeg/                    build glue (this directory)
  SConstruct                 builds ffmpeg-src/*.cpp against ../godot-cpp
  ffmpeg.gdextension         descriptor template ([dependencies] filled by the build)
  gen_yuv_shader.py           yuv_to_rgb.glsl -> gen/*.gen.h (upstream format)
  compat/                    godot-cpp master compat headers (see above)
dist/ffmpeg/               build output (gitignored; CI uploads it as an artifact)
  linux64/, macos/, win64/    portable bundles
.github/workflows/build-{linux,macos,windows}.yml   one job per platform each
```

The bundle tools are shared with the ocio project and parameterized via
environment variables:

```sh
export LIB_PREFIX=libgdffmpeg            # extension library basename
export ADDON_RES_BASE="res://addons/ffmpeg"
export GDEXTENSION_TEMPLATE="$PWD/ffmpeg/ffmpeg.gdextension"
```

## Build (Linux, native)

Requirements: `scons`, `pkg-config`, `patchelf`, FFmpeg >= 6 (dev headers).
conda-forge is the tested dependency source:

```sh
micromamba create -y -p ./ffenv -c conda-forge "ffmpeg=6.1.2=lgpl*" pkg-config
cd ffmpeg
PKG_CONFIG_PATH=../ffenv/lib/pkgconfig scons target=template_debug rpath=0
PKG_CONFIG_PATH=../ffenv/lib/pkgconfig scons target=template_release rpath=0
../ocio/tools/linux-bundle.sh stage libgdffmpeg.linux.template_release.x86_64.so \
  ../dist/ffmpeg/linux64 ../ffenv/lib
../ocio/tools/linux-bundle.sh verify libgdffmpeg.linux.template_release.x86_64.so \
  ../dist/ffmpeg/linux64
```

The bundle carries the extension's whole runtime closure with
`RUNPATH=$ORIGIN`, so it runs on a machine with nothing installed
(verified with `dlopen` after hiding the dependency prefix).

macOS/Windows follow the same pattern as the ocio README (universal lipo
+ `@rpath` + codesign on macOS; PE import closure from the conda
`Library/bin` on Windows with the `FFMPEG_CFLAGS`/`FFMPEG_LIBS` env-var
overrides, since Windows has no pkg-config).

## Consume

Copy `dist/ffmpeg/<platform>/` into the target project as
`addons/ffmpeg/<platform>/`, then compose the descriptor from the
per-platform snippets:

```sh
ocio/tools/compose-gdextension.sh <project>/addons/ffmpeg/ffmpeg.gdextension \
  macos:dist/ffmpeg/macos linux:dist/ffmpeg/linux64 windows:dist/ffmpeg/win64
```

## Validate

The classes are registered at SCENE level; in the editor:

```gdscript
print(ClassDB.class_exists("FFmpegVideoStream"))   # true
var s := FFmpegVideoStream.new()
```

Verified locally on Godot 4.7.2 headless: extension loads from the bundled
folder with the conda prefix hidden, `FFmpegVideoStream` registers, and the
codec dump prints.

## License notes

- EIRTeam.FFmpeg: MIT (ffmpeg-src submodule, LICENSE there).
- FFmpeg: LGPL-2.1-or-later, **LGPL build** (`ffmpeg=6.1.2=lgpl*`, no GPL
  components like libx264). Keep the LGPL text and the FFmpeg credit line
  in anything you distribute (the ideorama project already ships both).
- The bundled FFmpeg closure includes ISC/BSD/PNG-style deps (libass,
  dav1d, aom, opus, theora, vorbis, vpx, webp, xz, zstd, ...) — see the
  generated `dependencies-snippet.txt` for the exact set.
