# STATUS — session log & handoff (2026-10-31 → 2026-11-01)

What we did, the exact state at stop time, and what to do next.
Build-system documentation: `README.md`, `ocio/README.md`,
`ffmpeg/README.md`.

## Goal

`godot-vfx-platform` = build repo producing **portable** GDExtension
addons for our Godot 4.7 projects. Two extensions so far, each with its
own artifact (fully separated):

| Workflow (job) | Artifact | Bundle |
| --- | --- | --- |
| build-linux (ocio-linux) | `ocio-linux64` | `libgdocio` + ~106 .so, RUNPATH=$ORIGIN |
| build-linux (ffmpeg-linux) | `ffmpeg-linux64` | `libgdffmpeg` + ~48 .so (FFmpeg 6.1 LGPL) |
| build-macos (ocio-macos) | `ocio-macos-universal` | `libgdocio` universal + ~92 dylibs, @rpath, signed |
| build-macos (ffmpeg-macos) | `ffmpeg-macos-universal` | `libgdffmpeg` universal + FFmpeg dylibs |
| build-windows (ocio-windows) | `ocio-win64` | `libgdocio` + all imported DLLs incl. MSVC CRT |
| build-windows (ffmpeg-windows) | `ffmpeg-win64` | `libgdffmpeg` + FFmpeg DLLs |

Shared: `godot-cpp` submodule (master @ cedbd9e, `api_version=4.7` →
Godot 4.7 bindings), conda-forge deps pinned per platform (OCIO 2.6.0 /
OIIO 3.2.1.2 / FFmpeg 6.1.2 =lgpl*), bundle tools in `ocio/tools/`
parameterized by env vars (`LIB_PREFIX`, `ADDON_RES_BASE`,
`GDEXTENSION_TEMPLATE`; ocio = defaults).

## Timeline (all pushed to github.com/kevinloustau/godot-vfx-platform)

1. **OCIO macOS pipeline** — ported `ocio/` from ideorama, fixed the
   SConstruct (dist/ install, macOS dylib naming, `rpath=0`, default
   target with `install=0`), wrote `macos-bundle.sh` (stage/merge:
   @rpath rewrite, universal lipo, codesign, deps snippet) and
   `compose-gdextension.sh` (one descriptor from per-platform snippets).
   Homebrew turned out to no longer install x86_64 macOS → switched to
   conda-forge `osx-arm64` + `osx-64` envs (x86_64 slice never executed,
   no Rosetta). Green run 41249244919.
2. **OCIO into ideorama** — installed `dist/ocio/macos/` (95 files,
   LFS) + composed descriptor; verified in Godot 4.7.2 headless.
   ideorama commits `a7e8edb`, then portable Linux bundle `b6bf60a`,
   cleanup `f6c8e2c`.
3. **OCIO Windows** — MSVC + conda-forge win-64 (MSYS2 packages not
   installable on runners), `windows-bundle.py` (pefile import closure,
   MSVC CRT bundled from System32, `bcryptprimitives.dll` = system).
   Debugged: GCC-only flags vs MSVC (D8021), split -I flags (C1083),
   missing CONDA_PREFIX export, include flags via CPPPATH.
4. **Linux portability** — proven the old .so was build-machine-only
   (`dlopen` with /home/linuxbrew hidden → FAIL), wrote
   `linux-bundle.sh` (readelf closure, RUNPATH=$ORIGIN — ldd output was
   unusable on Ubuntu 26), fixed a real linker bug on the way
   (-l/-L in LINKFLAGS were dropped by --as-needed → LIBS/LIBPATH,
   now links with --no-undefined). Installed into ideorama, verified in
   Godot 4.7.2 with the conda prefix hidden.
5. **FFmpeg extension** — added `ffmpeg-src/` submodule
   (EIRTeam/EIRTeam.FFmpeg @ 84ea333, the release ideorama ships) +
   `ffmpeg/` build glue (SConstruct, `gen_yuv_shader.py`, `compat/`
   force-include for godot-cpp master: `Mutex`→`godot::CoreBind`,
   dropped `mutex_lock.hpp`). FFmpeg 6.1.2 LGPL from conda-forge on all
   platforms (same sonames as upstream: avcodec 60). Builds + loads
   locally (Godot 4.7.2 headless, FFmpegVideoStream registers). One job
   per platform added to each workflow; checkouts use `submodules: true`
   (EIRTeam's nested ffmpeg-kit is huge and unused).
6. **CI fixes** — `CONDA_PREFIX` not exported by setup-micromamba@v2
   (pinned explicitly in every job), dlopen smoke tests ran from the
   wrong cwd, and the **artifact separation bug**: the macOS ffmpeg
   merge step missed the per-project overrides → an extra
   `ocio.gdextension` (with ocio res:// paths) shipped inside the ffmpeg
   artifact. All overrides now live at job-level `env:` in the ffmpeg
   jobs (`1e842cb`, `43910c3`).

## Exact state at stop time

- godot-vfx-platform `main` @ `43910c3`, everything pushed.
- **In flight (started, not finished):** runs 41349313090 (build-linux),
  41349313091 (build-windows), 41349313104 (build-macos) — the first
  runs with fully separated artifacts. They had NOT completed when we
  stopped; older runs (1e842cb) were cancelled by concurrency.
- OCIO: Linux verified in Godot; macOS built+signed but **never tested
  on a real Mac**; Windows built, artifact not yet inspected/downloaded.
- FFmpeg: Linux verified locally; macOS/Windows builds not yet green in
  CI; **not yet integrated into ideorama at all**.
- ideorama: OCIO addon fully replaced (macos + linux64) and committed;
  **staged but uncommitted**: deletions of the old
  `godot/addons/ocio/linux64/libIex-3_5.so.34` etc. (old
  Homebrew-named libs) — commit them. The FFmpeg addon
  (`godot/addons/ffmpeg/`) still has the old upstream release binaries.
  Untracked user work I did not touch: `native/ocio/SConstruct` (M),
  `native/ocio/build-macos.sh`, `native/ocio/docker/macos/`.

## Next steps (in order)

1. Check the three in-flight runs:
   `gh run list -R kevinloustau/godot-vfx-platform`;
   `gh run watch <id> -R kevinloustau/godot-vfx-platform --exit-status`.
   If something fails: `gh run view --job=<job-id> --log | tail -50`,
   fix, push, re-watch.
2. Download and inspect each artifact — one `.gdextension` per artifact,
   matching its project:
   `gh run download <run-id> -n ffmpeg-macos-universal -D <dir>`;
   expected: `ffmpeg.gdextension`, `libgdffmpeg.macos.*.universal.dylib`,
   FFmpeg dylibs, `dependencies-snippet.txt` — **no ocio files**.
3. Commit the staged linux64 deletions in ideorama.
4. Integrate ffmpeg into ideorama (same recipe as OCIO):
   - artifacts → `godot/addons/ffmpeg/{linux64,macos,win64}/`
   - compose: `ocio/tools/compose-gdextension.sh
     <project>/addons/ffmpeg/ffmpeg.gdextension
     macos:dist/ffmpeg/macos linux:dist/ffmpeg/linux64
     windows:dist/ffmpeg/win64` (android entries are dropped)
   - smoke: `ClassDB.class_exists("FFmpegVideoStream")` in 4.7.2
   - commit (LFS rules already cover .dll/.so/.dylib).
5. Test both macOS artifacts on a real Mac.
6. Optional: shrink bundles (custom OIIO/FFmpeg builds without
   ffmpeg/heif video codecs, or static link) — macos ocio ≈306 MB,
   linux ocio ≈200 MB, ffmpeg ≈120 MB.

## Gotchas encoded in the pipeline (do not undo)

- Homebrew cannot install x86_64 macOS (2026) → conda-forge for both
  arches; x86_64 env is linked against, never executed.
- setup-micromamba@v2 does not export `CONDA_PREFIX` → each job pins it.
- `-L`/`-l` go to scons `LIBS`/`LIBPATH`, never `LINKFLAGS`
  (--as-needed drops them); Linux links with `--no-undefined`.
- godot-cpp master moved `Mutex` into `godot::CoreBind`, dropped
  `core/mutex_lock.hpp` → `ffmpeg/compat/compat_force.hpp` force-include
  keeps the EIRTeam submodule pristine.
- `bcryptprimitives.dll` is a system DLL; `vcruntime140`/`msvcp140` are
  NOT system DLLs — they are bundled (users may lack the VC++ redist).
- Smoke tests must hide/move the dependency prefix first; otherwise they
  prove nothing.
- `submodules: true` (never `recursive`) — ffmpeg-kit nested submodule.
- Bundle tools are shared: new project = new folder + env overrides
  (`LIB_PREFIX`, `ADDON_RES_BASE`, `GDEXTENSION_TEMPLATE`) at job level.
