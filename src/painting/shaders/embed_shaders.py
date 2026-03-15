#!/usr/bin/env python3
"""Convert .qsb shader files to a C++ header with embedded byte arrays."""
import sys
import os

def embed(path, var_name):
    with open(path, 'rb') as f:
        data = f.read()
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        lines.append('    ' + ', '.join(f'0x{b:02x}' for b in chunk))
    hex_body = ',\n'.join(lines)
    return (f'static constexpr unsigned char {var_name}[] = {{\n'
            f'{hex_body}\n'
            f'}};\n'
            f'static constexpr unsigned int {var_name}_len = {len(data)};\n')

if __name__ == '__main__':
    # Usage: embed_shaders.py output.h name1:input1.qsb name2:input2.qsb ...
    output_path = sys.argv[1]
    pairs = sys.argv[2:]
    with open(output_path, 'w') as out:
        out.write('#pragma once\n\n')
        out.write('// Auto-generated from .qsb shader files. Do not edit.\n\n')
        for pair in pairs:
            var_name, input_path = pair.split(':', 1)
            out.write(embed(input_path, var_name))
            out.write('\n')
