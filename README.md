# godot-vfx-platform

GDExtension builds for the VFX tools used by our Godot projects — starting
with **OCIO** (OpenColorIO + OpenImageIO). Each extension lives in its own
folder and builds through GitHub Actions into portable artifacts.

| Extension | What it provides | CI |
| --- | --- | --- |
| [`ocio/`](ocio/) | `OcioTransform`: exact OCIO color transforms, OCIO-generated GPU shaders, float EXR I/O | `.github/workflows/build-macos.yml` (universal) |

Targets the **Godot 4.7** API (`godot-cpp` master submodule, `api_version=4.7`).

## Building

```sh
git clone --recursive https://github.com/kevinloustau/godot-vfx-platform
cd godot-vfx-platform/ocio
scons target=template_release     # platform-specific steps in ocio/README.md
```

Binaries + a ready-to-use `ocio.gdextension` install into `dist/ocio/`;
the macOS workflow produces a portable universal bundle (bundled
Homebrew dylibs rewritten to `@rpath`, ad-hoc signed) as the
`ocio-macos-universal` artifact.

## Using an artifact in a Godot project

Copy the platform folder and descriptor into the target project:

```
<project>/addons/ocio/ocio.gdextension          # from dist/ocio (macOS: the generated one)
<project>/addons/ocio/macos/                   # artifact contents (or linux64/, win64/)
```

See [`ocio/README.md`](ocio/README.md) for the full build, bundling and
validation documentation.
