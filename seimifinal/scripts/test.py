#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
华为嵌入式软件大赛复赛 - 简化测试脚本
快速运行测试，最少的输出信息
"""

import subprocess
import sys
import os
from pathlib import Path

# 重定向工作目录为当前脚本所在目录
os.chdir(Path(__file__).parent)


def main():
    """简化的测试主函数"""
    # 获取输入文件名
    input_filename = sys.argv[1] if len(sys.argv) > 1 else "data.in"
    output_filename = "output.out"

    print(f"运行测试: main.exe < {input_filename} > {output_filename}")

    # 检查文件
    if not Path("main.exe").exists():
        print("❌ main.exe 不存在")
        return

    if not Path(input_filename).exists():
        print(f"❌ {input_filename} 不存在")
        return

    try:
        # 运行程序
        with open(input_filename, "r", encoding="utf-8") as infile:
            with open(output_filename, "w", encoding="utf-8") as outfile:
                result = subprocess.run(
                    ["./main.exe"],
                    stdin=infile,
                    stdout=outfile,
                    stderr=subprocess.PIPE,
                    timeout=30,
                    text=True,
                )

        if result.returncode == 0:
            # 统计输出行数
            with open(output_filename, "r", encoding="utf-8") as f:
                line_count = len(f.readlines())
            print(f"✅ 成功! 输出 {line_count} 行到 {output_filename}")
        else:
            print(f"❌ 程序返回错误码: {result.returncode}")
            if result.stderr:
                print(f"错误: {result.stderr}")

    except subprocess.TimeoutExpired:
        print("❌ 超时 (>30秒)")
    except Exception as e:
        print(f"❌ 执行失败: {e}")


if __name__ == "__main__":
    main()
