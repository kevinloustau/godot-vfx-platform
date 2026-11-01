#!/usr/bin/env python3
"""Windows bundling for the OCIO GDExtension.

Windows has no rpath: the loader only searches the executable's directory
(and PATH), so every non-system DLL the extension imports must ship next to
it. This walks the PE import table transitively and copies the closure.

  python windows-bundle.py stage <main.dll> <out-dir> <dep-bin-dir>
      Copy the extension plus the full import closure into <out-dir>,
      resolving DLLs from <dep-bin-dir> (conda env Library/bin).
      Windows system DLLs are never bundled.

  python windows-bundle.py verify <main.dll> <out-dir>
      Fail unless every import of every bundled DLL is either a system DLL
      or present in <out-dir>.

Needs `pefile` (pip install pefile).
"""

import os
import shutil
import sys

try:
    import pefile
except ImportError:  # pragma: no cover
    sys.exit("windows-bundle: pefile is required (pip install pefile)")

# Per-project overrides (defaults: the ocio extension).
LIB_PREFIX = os.environ.get("LIB_PREFIX", "libgdocio")
ADDON_RES_BASE = os.environ.get("ADDON_RES_BASE", "res://addons/ocio")

# DLLs that always come from Windows itself (lowercase).
SYSTEM_DLLS = {
    "kernel32.dll", "kernelbase.dll", "ntdll.dll", "msvcrt.dll",
    "ucrtbase.dll", "user32.dll", "gdi32.dll", "gdi32full.dll",
    "advapi32.dll", "shell32.dll", "shlwapi.dll", "ole32.dll",
    "oleaut32.dll", "comdlg32.dll", "comctl32.dll", "ws2_32.dll",
    "wsock32.dll", "mswsock.dll", "crypt32.dll", "bcrypt.dll",
    "secur32.dll", "iphlpapi.dll", "version.dll", "psapi.dll",
    "setupapi.dll", "cfgmgr32.dll", "imm32.dll", "winmm.dll",
    "uxtheme.dll", "dwmapi.dll", "rpcrt4.dll", "userenv.dll",
    "netapi32.dll", "mpr.dll", "dbghelp.dll", "d3d11.dll", "d3d12.dll",
    "dxgi.dll", "opengl32.dll", "glu32.dll", "wintrust.dll",
    "powrprof.dll", "wtsapi32.dll", "msimg32.dll", "gdiplus.dll",
    "dnsapi.dll", "winspool.drv", "winhttp.dll", "wininet.dll",
    "bcryptprimitives.dll", "mswsock.dll",
}
SYSTEM_PREFIXES = ("api-ms-win-", "ext-ms-")


def is_system_dll(name: str) -> bool:
    n = name.lower()
    if n in REDIST_DLLS:
        return False  # bundled: they must ship with the addon
    return n in SYSTEM_DLLS or n.startswith(SYSTEM_PREFIXES)


def imports_of(path: str):
    """Lowercase DLL names imported by a PE file."""
    try:
        pe = pefile.PE(path, fast_load=True)
        pe.parse_data_directories(
            directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]]
        )
    except Exception as exc:  # noqa: BLE001 - report and stop
        raise SystemExit(f"windows-bundle: cannot read imports of {path}: {exc}")
    names = set()
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []) or []:
        names.add(entry.dll.decode("ascii", "replace").lower())
    pe.close()
    return sorted(names)


# MSVC CRT redistributable DLLs: bundled (users may lack the VC++
# redistributable), resolved from the system dir of the build machine.
# NOT in SYSTEM_DLLS - they must ship with the addon.
REDIST_DLLS = {
    "vcruntime140.dll", "vcruntime140_1.dll", "vcruntime140d.dll",
    "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll",
}
SYSTEM_DIRS = ("C:/Windows/System32",)


def find_in(dirpath: str, name: str):
    """Case-insensitive lookup of a DLL inside one directory."""
    if not os.path.isdir(dirpath):
        return None
    for entry in os.listdir(dirpath):
        if entry.lower() == name.lower():
            return os.path.join(dirpath, entry)
    return None


def stage(main_dll: str, out_dir: str, dep_bin: str) -> int:
    if not os.path.isfile(main_dll):
        raise SystemExit(f"windows-bundle: library not found: {main_dll}")
    if not os.path.isdir(dep_bin):
        raise SystemExit(f"windows-bundle: dependency bin dir not found: {dep_bin}")
    os.makedirs(out_dir, exist_ok=True)

    name = os.path.basename(main_dll)
    shutil.copy2(main_dll, os.path.join(out_dir, name))

    queue = [os.path.join(out_dir, name)]
    bundled = []
    while queue:
        current = queue.pop(0)
        for dll in imports_of(current):
            if is_system_dll(dll):
                continue
            if find_in(out_dir, dll):
                continue
            src = find_in(dep_bin, dll)
            if not src and dll in REDIST_DLLS:
                for d in SYSTEM_DIRS:
                    src = find_in(d, dll)
                    if src:
                        break
            if not src:
                raise SystemExit(
                    f"windows-bundle: cannot resolve {dll} "
                    f"(needed by {os.path.basename(current)})"
                )
            dst = os.path.join(out_dir, os.path.basename(src))
            shutil.copy2(src, dst)
            bundled.append(os.path.basename(src))
            queue.append(dst)

    print(f"staged {name} + {len(bundled)} DLLs in {out_dir}")
    write_snippet(out_dir)
    return 0


def verify(main_dll: str, out_dir: str) -> int:
    if not os.path.isfile(main_dll):
        raise SystemExit(f"windows-bundle: library not found: {main_dll}")
    out_dir = os.path.abspath(out_dir)
    bad = 0
    dlls = sorted(f for f in os.listdir(out_dir) if f.lower().endswith(".dll"))
    for dll in dlls:
        for dep in imports_of(os.path.join(out_dir, dll)):
            if is_system_dll(dep):
                continue
            if not find_in(out_dir, dep):
                print(f"  {dll} imports {dep}, not bundled")
                bad += 1
    if bad:
        raise SystemExit(f"windows-bundle: {bad} unresolved imports in {out_dir}")
    print(f"verified: {len(dlls)} DLLs, all imports bundled or system")
    return 0


def write_snippet(out_dir: str) -> None:
    snippet = os.path.join(out_dir, "dependencies-snippet.txt")
    deps = sorted(f for f in os.listdir(out_dir)
                  if f.lower().endswith(".dll") and not f.lower().startswith(LIB_PREFIX))
    with open(snippet, "w", encoding="utf-8") as handle:
        handle.write("; Generated by ocio/tools/windows-bundle.py — Windows "
                     "[dependencies]\n")
        handle.write("; section for ocio.gdextension (bundled DLLs, copied next to the\n")
        handle.write("; exported executable).\n")
        handle.write("windows.x86_64 = {\n")
        for dep in deps:
            handle.write(f'\t"{ADDON_RES_BASE}/win64/{dep}": "",\n')
        handle.write("}\n")
    print(f"wrote {snippet}")


def main(argv) -> int:
    if len(argv) < 2:
        raise SystemExit(__doc__)
    cmd = argv[1]
    if cmd == "stage" and len(argv) == 5:
        return stage(argv[2], argv[3], argv[4])
    if cmd == "verify" and len(argv) == 4:
        return verify(argv[2], argv[3])
    raise SystemExit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
