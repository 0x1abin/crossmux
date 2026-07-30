#!/usr/bin/env python3
"""将 .sok 文件转换为推箱子二进制关卡文件 (levels.bin) 和 C 头文件 (levels_data.h)"""
import sys
import os

INPUT_FILE = "levels.sok"
OUTPUT_BIN = "levels.bin"
OUTPUT_HEADER = "levels_data.h"

CELL_MAP = {
    '#': 1,  # WALL
    ' ': 2,  # FLOOR
    '.': 3,  # TARGET
    '$': 4,  # BOX
    '*': 5,  # BOX_ON_TARGET
    '@': 6,  # PLAYER
    '+': 7,  # PLAYER_ON_TARGET
}

def parse_sok(filepath):
    levels = []
    current = []
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            line = line.rstrip('\n\r')
            if line.startswith(';') or line.startswith('Title:') or \
               line.startswith('Author:') or line.startswith('Date:') or \
               line.startswith('Set:') or line.startswith('Copyright:') or \
               line.startswith('Email:') or line.startswith('Homepage:'):
                continue
            if not line.strip():
                if current:
                    levels.append(current)
                    current = []
                continue
            if line and line[0] in ('#', ' ', '.', '$', '@', '*', '+', '-'):
                current.append(line)
        if current:
            levels.append(current)
    return levels

def convert(levels, bin_path, header_path):
    # 构建二进制数据
    bin_data = bytearray()
    for level in levels:
        width = max(len(row) for row in level)
        height = len(level)
        bin_data.append(height)
        bin_data.append(width)
        for row in level:
            for ch in row.ljust(width):  # 补齐空格
                val = CELL_MAP.get(ch, 2)  # 未知字符当作地板
                bin_data.append(val)

    # 写入 .bin 文件
    with open(bin_path, 'wb') as f:
        f.write(bin_data)
    print(f"✅ 已生成 {bin_path}，共 {len(levels)} 关，{len(bin_data)} 字节")

    # 写入 .h 文件
    with open(header_path, 'w', encoding='utf-8') as f:
        f.write(f"// Auto-generated from {os.path.basename(INPUT_FILE)}\n")
        f.write("#pragma once\n")
        f.write("#include <cstdint>\n\n")
        f.write(f"constexpr uint8_t kLevelsBin[] = {{\n")
        for i in range(0, len(bin_data), 16):
            chunk = bin_data[i:i+16]
            hex_str = ", ".join(f"0x{b:02X}" for b in chunk)
            f.write(f"  {hex_str},\n")
        f.write("};\n\n")
        f.write(f"constexpr size_t kLevelsBinSize = {len(bin_data)};\n")
    print(f"✅ 已生成 {header_path}")

if __name__ == "__main__":
    if not os.path.exists(INPUT_FILE):
        print(f"❌ 未找到 {INPUT_FILE}")
        sys.exit(1)
    levels = parse_sok(INPUT_FILE)
    print(f"共解析 {len(levels)} 关")
    convert(levels, OUTPUT_BIN, OUTPUT_HEADER)