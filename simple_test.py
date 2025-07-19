#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
简单测试脚本 - 直接测试华为大赛C++代码
"""

import subprocess
import sys
import os
from pathlib import Path


def simple_test():
    """简单直接的测试"""

    print("=" * 50)
    print("华为嵌入式大赛C++代码测试")
    print("=" * 50)

    # 检查文件路径
    cpp_file = "preliminary/src/1.0.2_highest/main.cpp"
    data_file = "preliminary/src/data.in"
    exe_file = "preliminary/src/1.0.2_highest/main.exe"

    if not os.path.exists(cpp_file):
        print(f"❌ 找不到C++文件: {cpp_file}")
        return False

    if not os.path.exists(data_file):
        print(f"❌ 找不到数据文件: {data_file}")
        return False

    print(f"✅ 找到C++文件: {cpp_file}")
    print(f"✅ 找到数据文件: {data_file}")

    # 编译
    print("\n🔨 开始编译...")
    try:
        result = subprocess.run(
            ["g++", "-std=c++17", "-O2", "-Wall", cpp_file, "-o", exe_file],
            capture_output=True,
            text=True,
            timeout=30,
        )

        if result.returncode == 0:
            print("✅ 编译成功!")
        else:
            print("❌ 编译失败!")
            print("错误信息:")
            print(result.stderr)
            return False

    except Exception as e:
        print(f"❌ 编译出错: {e}")
        return False

    # 运行
    print("\n🏃 开始运行...")
    try:
        with open(data_file, "r", encoding="utf-8") as f:
            input_data = f.read()

        result = subprocess.run(
            [exe_file], input=input_data, capture_output=True, text=True, timeout=60
        )

        if result.returncode == 0:
            print("✅ 运行成功!")

            # 保存输出
            output_file = "preliminary/src/1.0.2_highest/output.txt"
            with open(output_file, "w", encoding="utf-8") as f:
                f.write(result.stdout)

            print(f"📄 输出已保存到: {output_file}")

            # 输出统计
            lines = result.stdout.strip().split("\n")
            print(f"📊 输出行数: {len(lines)}")
            print(f"📊 输出字符数: {len(result.stdout)}")

            # 预览前5行
            print(f"\n📋 输出预览:")
            for i, line in enumerate(lines[:5], 1):
                print(f"   {i}: {line}")
            if len(lines) > 5:
                print(f"   ... (还有 {len(lines)-5} 行)")

        else:
            print(f"❌ 运行失败! (返回码: {result.returncode})")
            if result.stderr:
                print("错误信息:")
                print(result.stderr)
            return False

    except Exception as e:
        print(f"❌ 运行出错: {e}")
        return False

    print(f"\n🎉 测试完成!")
    return True


if __name__ == "__main__":
    if simple_test():
        print(f"\n✨ 所有测试通过!")
        sys.exit(0)
    else:
        print(f"\n❌ 测试失败!")
        sys.exit(1)
