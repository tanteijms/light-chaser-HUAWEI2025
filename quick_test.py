#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
华为嵌入式大赛 - 快速测试脚本
简化版本，用于快速编译和运行测试
"""

import subprocess
import sys
import time
from pathlib import Path


def quick_test():
    """快速测试函数"""

    # 定义文件路径
    cpp_file = Path("preliminary/src/1.0.2_highest/main.cpp")
    data_file = Path("preliminary/src/data.in")
    exe_file = Path("preliminary/src/1.0.2_highest/main.exe")

    print("🚀 华为嵌入式大赛代码快速测试")
    print("=" * 50)

    # 检查文件是否存在
    if not cpp_file.exists():
        print(f"❌ C++源文件不存在: {cpp_file}")
        return False

    if not data_file.exists():
        print(f"❌ 测试数据文件不存在: {data_file}")
        return False

    print(f"📁 C++文件: {cpp_file}")
    print(f"📁 数据文件: {data_file}")

    # 编译C++文件
    print(f"\n🔨 编译中...")
    compile_cmd = [
        "g++",
        "-std=c++17",
        "-O2",
        "-Wall",
        str(cpp_file),
        "-o",
        str(exe_file),
    ]

    try:
        start_time = time.time()
        compile_result = subprocess.run(
            compile_cmd, capture_output=True, text=True, timeout=30
        )
        compile_time = time.time() - start_time

        if compile_result.returncode == 0:
            print(f"✅ 编译成功 (用时: {compile_time:.2f}秒)")
        else:
            print(f"❌ 编译失败!")
            print(f"错误信息:\n{compile_result.stderr}")
            return False

    except subprocess.TimeoutExpired:
        print(f"❌ 编译超时!")
        return False
    except FileNotFoundError:
        print(f"❌ 找不到g++编译器!")
        print("请安装MinGW或确保g++在PATH中")
        return False

    # 运行程序
    print(f"\n🏃 运行程序...")

    try:
        # 读取输入数据
        with open(data_file, "r", encoding="utf-8") as f:
            input_data = f.read()

        start_time = time.time()
        run_result = subprocess.run(
            [str(exe_file)],
            input=input_data,
            capture_output=True,
            text=True,
            timeout=60,  # 60秒运行超时
            cwd=exe_file.parent,
        )
        run_time = time.time() - start_time

        if run_result.returncode == 0:
            print(f"✅ 运行成功 (用时: {run_time:.2f}秒)")

            # 保存输出
            output_file = exe_file.parent / "output.txt"
            with open(output_file, "w", encoding="utf-8") as f:
                f.write(run_result.stdout)

            # 输出统计信息
            output_lines = len(run_result.stdout.strip().split("\n"))
            output_chars = len(run_result.stdout)

            print(f"📊 输出统计:")
            print(f"   - 输出行数: {output_lines}")
            print(f"   - 输出字符数: {output_chars}")
            print(f"   - 输出已保存到: {output_file}")

            # 显示前几行输出作为预览
            output_preview = run_result.stdout.strip().split("\n")[:5]
            print(f"\n📋 输出预览 (前5行):")
            for i, line in enumerate(output_preview, 1):
                print(f"   {i}: {line}")
            if output_lines > 5:
                print(f"   ... (还有 {output_lines - 5} 行)")

        else:
            print(f"❌ 运行失败 (返回码: {run_result.returncode})")
            if run_result.stderr:
                print(f"错误信息:\n{run_result.stderr}")
            if run_result.stdout:
                print(f"程序输出:\n{run_result.stdout}")
            return False

    except subprocess.TimeoutExpired:
        print(f"❌ 程序运行超时 (>60秒)")
        return False
    except Exception as e:
        print(f"❌ 运行出错: {e}")
        return False

    print(f"\n🎉 测试完成!")
    return True


def main():
    """主函数"""
    success = quick_test()

    if success:
        print(f"\n✨ 所有测试通过! 代码可以正常运行。")
        sys.exit(0)
    else:
        print(f"\n⚠️  测试失败，请检查代码问题。")
        sys.exit(1)


if __name__ == "__main__":
    main()
