# godot-vfx-platform

GDExtension builds for the VFX tools used by our Godot projects — starting
with **OCIO** (OpenColorIO + OpenImageIO). Each extension lives in its own
folder and builds through GitHub Actions into portable artifacts.

| Extension | What it provides | CI |
| --- | --- | --- |
| [`ocio/`](ocio/) | `OcioTransform`: exact OCIO color transforms, OCIO-generated GPU shaders, float EXR I/O | `build-linux.yml`, `build-macos.yml`, `build-windows.yml` (all portable) |

Targets the **Godot 4.7** API (`godot-cpp` master submodule, `api_version=4.7`).

## Building

```sh
git clone --recursive https://github.com/kevinloustau/godot-vfx-platform
cd godot-vfx-platform/ocio
scons target=template_release     # platform-specific steps in ocio/README.md
```

Binaries + a ready-to-use `ocio.gdextension` install into `dist/ocio/`.
Both CI artifacts are **self-contained** — the extension ships its whole
dependency closure, so a user needs nothing installed:

- `ocio-linux64`: extension + 57 `.so` files, all `RUNPATH=$ORIGIN`
- `ocio-macos-universal`: universal (arm64+x86_64) dylibs rewritten to
  `@rpath`, ad-hoc signed, deps exported to `Contents/Frameworks`
- `ocio-win64`: extension + every imported DLL (no rpath on Windows, so
  they ship next to the executable)

## Using an artifact in a Godot project

Copy each platform folder into the target project, then compose the
descriptor from the per-platform dependency snippets:

```
<project>/addons/ocio/macos/     # artifact contents
<project>/addons/ocio/linux64/   # artifact contents
ocio/tools/compose-gdextension.sh <project>/addons/ocio/ocio.gdextension \
  macos:dist/ocio/macos linux:dist/ocio/linux64
```

See [`ocio/README.md`](ocio/README.md) for the full build, bundling and
validation documentation.
