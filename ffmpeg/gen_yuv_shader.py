#!/usr/bin/env python3
"""Generate a C header from a GLSL file (byte array), the same format as
EIRTeam.FFmpeg's gdextension_build/glsl_builders.py, which the extension's
sources #include as "yuv_to_rgb.glsl.gen.h".

Usage: gen_yuv_shader.py <input.glsl> <output.glsl.gen.h>
"""

import os
import sys


def include_file_in_raw_header(filename, code):
    with open(filename, "r") as handle:
        line = handle.readline()
        while line:
            while "#include " in line:
                includeline = line.replace("#include ", "").strip()[1:-1]
                included = os.path.relpath(
                    os.path.join(os.path.dirname(filename), includeline)
                )
                include_file_in_raw_header(included, code)
                line = handle.readline()
            code.append(line)
            line = handle.readline()


def build_raw_header(filename, out_file):
    code = []
    include_file_in_raw_header(filename, code)

    text = "".join(code)
    inline = ",".join(str(ord(c)) for c in text) + ",0"

    base = os.path.basename(out_file).replace(".glsl.gen.h", "_shader_glsl")
    guard = base.replace(".", "_").upper()

    with open(out_file, "w") as handle:
        handle.write(f"""/* WARNING, THIS FILE WAS GENERATED, DO NOT EDIT */
#ifndef {guard}_RAW_H
#define {guard}_RAW_H

static const char {base}[] = {{
    {inline}
}};
#endif
""")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    build_raw_header(sys.argv[1], sys.argv[2])
