#!/usr/bin/env python3
"""将 .sok 文件转换为推箱子二进制关卡文件 (levels.bin)"""
import sys
import os

INPUT_FILE = "levels.sok"
OUTPUT_FILE = "levels.bin"

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

def convert(levels, output):
    with open(output, 'wb') as f:
        for level in levels:
            # 计算最大宽度
            width = max(len(row) for row in level)
            height = len(level)
            # 写入高度和宽度（各1字节）
            f.write(bytes([height, width]))
            for row in level:
                for ch in row.ljust(width):  # 补齐空格
                    val = CELL_MAP.get(ch, 2)  # 未知字符当作地板
                    f.write(bytes([val]))
    print(f"✅ 已生成 {output}，共 {len(levels)} 关")

if __name__ == "__main__":
    if not os.path.exists(INPUT_FILE):
        print(f"❌ 未找到 {INPUT_FILE}")
        sys.exit(1)
    levels = parse_sok(INPUT_FILE)
    print(f"共解析 {len(levels)} 关")
    convert(levels, OUTPUT_FILE)