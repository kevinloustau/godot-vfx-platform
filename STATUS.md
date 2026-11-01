# Status / handoff — 2026-11-01

Where things stand, what was just changed, and what is left to do.
Details of the build system itself are in `README.md` and
`ocio/README.md` / `ffmpeg/README.md`.

## What this repo does

Builds GDExtensions for our Godot 4.7 projects into **portable** addons —
each artifact carries the extension's whole runtime dependency closure, so
users need nothing installed:

| Job | Artifact | Contents |
| --- | --- | --- |
| ocio-linux | `ocio-linux64` | `libgdocio` + ~106 `.so` (`RUNPATH=$ORIGIN`) |
| ocio-macos | `ocio-macos-universal` | `libgdocio` universal + ~92 dylibs (`@rpath`, signed) |
| ocio-windows | `ocio-win64` | `libgdocio` + every imported DLL incl. MSVC CRT |
| ffmpeg-linux | `ffmpeg-linux64` | `libgdffmpeg` + ~48 `.so` (FFmpeg 6.1 LGPL closure) |
| ffmpeg-macos | `ffmpeg-macos-universal` | `libgdffmpeg` universal + FFmpeg dylibs |
| ffmpeg-windows | `ffmpeg-win64` | `libgdffmpeg` + FFmpeg DLLs |

Two projects, shared pipeline: `ocio/` (sources in-repo) and `ffmpeg/`
(sources = `ffmpeg-src/` submodule, EIRTeam.FFmpeg @ `84ea333`). The
bundle tools in `ocio/tools/` are project-parameterized via env vars
(`LIB_PREFIX`, `ADDON_RES_BASE`, `GDEXTENSION_TEMPLATE`; ocio defaults).
Dependencies are pinned from conda-forge for all platforms (OCIO 2.6.0 /
OIIO 3.2.1.2 / FFmpeg 6.1.2 LGPL).

## What works (verified)

- **OCIO Linux**: builds for the Godot 4.7 API (godot-cpp master,
  `api_version=4.7`), bundles, `dlopen`s with the dependency prefix
  hidden; installed and smoke-tested in the ideorama project on Godot
  4.7.2 (OcioTransform registered, OCIO 2.6.0, 39 color spaces).
- **OCIO macOS**: green CI run 41249244919 produced
  `ocio-macos-universal`; installed into ideorama
  (`godot/addons/ocio/macos/`, 95 files, LFS). **Still untested on an
  actual Mac** — that is the one remaining verification gap.
- **FFmpeg Linux**: builds locally, bundles, loads in Godot 4.7.2
  headless from a scratch project with the conda prefix hidden
  (FFmpegVideoStream registers, codec dump prints).
- ideorama commits so far: macOS OCIO bundle (`a7e8edb`), portable Linux
  OCIO bundle + multi-platform descriptor (`b6bf60a`, `f6c8e2c`).

## The bug fixed in this commit (answer to "why did the ffmpeg artifact
contain ocio.gdextension?")

The macOS ffmpeg job exported the per-project overrides
(`LIB_PREFIX/ADDON_RES_BASE/GDEXTENSION_TEMPLATE`) only in the *stage*
and *compose* steps. The *merge* step missed them, so `write_dist` fell
back to the ocio defaults and wrote an extra **`ocio.gdextension`** (with
`res://addons/ocio/...` paths) into `dist/ffmpeg/macos`, which shipped in
the artifact next to the correct `ffmpeg.gdextension`. Not normal — now
the overrides live at the **job level** (`env:`) so every step sees them.
If you downloaded a ffmpeg artifact from an earlier run, re-download
after the next green run: it must contain only `ffmpeg.gdextension`,
`libgdffmpeg.*`, the FFmpeg dylibs and `dependencies-snippet.txt`.

## Open items (next session)

1. **Watch the three runs** this push triggers (build-linux/-macos/
   -windows, one ocio + one ffmpeg job each). Expect conda env solves +
   ~15 min Linux, ~20 min macOS/Windows per job. gh commands:
   `gh run watch <id> -R kevinloustau/godot-vfx-platform
   --exit-status`; logs with `gh run view --job=<id> --log`.
2. **Re-download + verify** the six artifacts (each must contain exactly
   one `.gdextension`, matching the artifact's project).
3. **Integrate ffmpeg into ideorama** like OCIO was:
   - copy the artifact platform dirs into
     `godot/addons/ffmpeg/{linux64,macos,win64}/`
   - compose the descriptor:
     `ocio/tools/compose-gdextension.sh <project>/addons/ffmpeg/ffmpeg.gdextension
     macos:dist/ffmpeg/macos linux:dist/ffmpeg/linux64 windows:dist/ffmpeg/win64`
     (replaces the old static DLL list; android entries are dropped —
     we don't build android)
   - smoke test in Godot (ClassDB.class_exists("FFmpegVideoStream"))
   - ideorama's `linux64` currently has the old EIRTeam release libs;
     our bundle replaces them (same sonames, avcodec 60).
   - commit with LFS (`*.dll/*.so/*.dylib` rules already exist).
4. **Test the macOS artifacts on a real Mac** (both addons).
5. Optional / known rough edges:
   - bundles are heavy (conda OIIO pulls ffmpeg/heif: macos ocio ≈306 MB;
     linux ocio ≈200 MB; ffmpeg ≈120 MB). Trimming means a custom OIIO
     build without video codecs, or static linking.
   - Linux `ubuntu-latest` migrates to Ubuntu 26 in Oct 2026 (glibc of
     the exported binaries tracks the runner).
   - `submodules: true` (not `recursive`) in every workflow is required:
     EIRTeam's nested `ffmpeg-kit` submodule is huge and unused.
   - the compose step regenerates descriptors; when adding a third
     project, copy the ffmpeg job env block and adjust the three vars.

## Lessons encoded in the pipeline (why it looks the way it does)

- Homebrew cannot install x86_64 macOS anymore → conda-forge provides
  matching `osx-arm64`/`osx-64` packages; the x86_64 slice is only
  linked against, never executed (no Rosetta needed).
- `setup-micromamba@v2` does **not** export `CONDA_PREFIX` to later
  steps → every job pins the prefix itself (`MAMBA_ROOT_PREFIX/envs/...`
  on Linux, `C:/Users/runneradmin/micromamba/envs/...` via `GITHUB_ENV`
  on Windows).
- `-L`/`-l` flags go to scons `LIBS`/`LIBPATH`, never `LINKFLAGS`
  (Ubuntu's `--as-needed` silently dropped them → extension with
  undefined symbols). Linux links with `--no-undefined` so this can
  never happen silently again.
- godot-cpp master moved `Mutex` into `godot::CoreBind` and dropped
  `core/mutex_lock.hpp` → `ffmpeg/compat/compat_force.hpp` force-include
  keeps the EIRTeam submodule unpatched.
- Windows: `bcryptprimitives.dll` is a system DLL; `vcruntime140`/
  `msvcp140` are NOT system DLLs — they are bundled from the build
  machine's System32 (users may lack the VC++ redistributable).
- Smoke tests always run with the dependency prefix **moved away** /
  hidden from the loader; a green test with the env still present proves
  nothing.
