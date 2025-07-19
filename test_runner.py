#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
C++ 华为嵌入式软件大赛代码测试脚本
用于测试C++代码的编译和运行状态
"""

import os
import subprocess
import time
import sys
from pathlib import Path


class CPPTester:
    def __init__(self, project_root):
        """
        初始化测试器

        Args:
            project_root: 项目根目录路径
        """
        self.project_root = Path(project_root)
        self.cpp_files = []
        self.test_cases = []
        self.results = []

    def find_cpp_files(self):
        """查找所有的C++源文件"""
        cpp_files = []

        # 查找所有main.cpp文件
        for root, dirs, files in os.walk(self.project_root):
            for file in files:
                if file == "main.cpp":
                    cpp_files.append(Path(root) / file)

        self.cpp_files = cpp_files
        print(f"找到 {len(cpp_files)} 个C++源文件:")
        for i, file in enumerate(cpp_files, 1):
            print(f"  {i}. {file}")

        return cpp_files

    def find_test_data(self):
        """查找所有的测试数据文件"""
        data_files = []

        # 查找所有data.in文件
        for root, dirs, files in os.walk(self.project_root):
            for file in files:
                if file == "data.in":
                    data_files.append(Path(root) / file)

        self.test_cases = data_files
        print(f"找到 {len(data_files)} 个测试数据文件:")
        for i, file in enumerate(data_files, 1):
            print(f"  {i}. {file}")

        return data_files

    def compile_cpp(self, cpp_file, output_name=None):
        """
        编译C++文件

        Args:
            cpp_file: C++源文件路径
            output_name: 输出可执行文件名称

        Returns:
            tuple: (是否成功, 可执行文件路径, 错误信息)
        """
        cpp_path = Path(cpp_file)
        if output_name is None:
            output_name = cpp_path.stem + ".exe"

        exe_path = cpp_path.parent / output_name

        # 编译命令
        compile_cmd = [
            "g++",
            "-std=c++17",
            "-O2",
            "-Wall",
            "-Wextra",
            str(cpp_path),
            "-o",
            str(exe_path),
        ]

        try:
            print(f"编译 {cpp_path.name}...")
            result = subprocess.run(
                compile_cmd, capture_output=True, text=True, timeout=60  # 60秒编译超时
            )

            if result.returncode == 0:
                print(f"✅ 编译成功: {exe_path}")
                return True, exe_path, ""
            else:
                print(f"❌ 编译失败: {cpp_path}")
                print(f"错误信息: {result.stderr}")
                return False, None, result.stderr

        except subprocess.TimeoutExpired:
            return False, None, "编译超时"
        except FileNotFoundError:
            return False, None, "找不到g++编译器，请确保已安装MinGW或其他C++编译器"
        except Exception as e:
            return False, None, str(e)

    def run_cpp_with_input(self, exe_path, input_file, timeout=30):
        """
        使用指定输入文件运行C++程序

        Args:
            exe_path: 可执行文件路径
            input_file: 输入文件路径
            timeout: 运行超时时间(秒)

        Returns:
            tuple: (是否成功, 输出内容, 错误信息, 运行时间)
        """
        try:
            print(f"运行 {exe_path.name} 使用输入文件 {input_file.name}...")

            with open(input_file, "r", encoding="utf-8") as f:
                input_data = f.read()

            start_time = time.time()
            result = subprocess.run(
                [str(exe_path)],
                input=input_data,
                capture_output=True,
                text=True,
                timeout=timeout,
                cwd=exe_path.parent,
            )
            end_time = time.time()

            runtime = end_time - start_time

            if result.returncode == 0:
                print(f"✅ 运行成功 (用时: {runtime:.2f}秒)")
                return True, result.stdout, "", runtime
            else:
                print(f"❌ 运行失败 (返回码: {result.returncode})")
                print(f"错误信息: {result.stderr}")
                return False, result.stdout, result.stderr, runtime

        except subprocess.TimeoutExpired:
            runtime = timeout
            return False, "", f"程序运行超时 (>{timeout}秒)", runtime
        except Exception as e:
            return False, "", str(e), 0

    def save_output(self, output_content, output_file):
        """保存输出到文件"""
        try:
            with open(output_file, "w", encoding="utf-8") as f:
                f.write(output_content)
            print(f"输出已保存到: {output_file}")
        except Exception as e:
            print(f"保存输出失败: {e}")

    def run_full_test(self):
        """运行完整的测试流程"""
        print("=" * 60)
        print("开始C++代码测试")
        print("=" * 60)

        # 查找文件
        cpp_files = self.find_cpp_files()
        data_files = self.find_test_data()

        if not cpp_files:
            print("❌ 没有找到C++源文件")
            return False

        if not data_files:
            print("❌ 没有找到测试数据文件")
            return False

        # 测试每个C++文件
        overall_success = True

        for cpp_file in cpp_files:
            print(f"\n{'='*40}")
            print(f"测试文件: {cpp_file}")
            print(f"{'='*40}")

            # 编译
            compile_success, exe_path, compile_error = self.compile_cpp(cpp_file)

            if not compile_success:
                print(f"跳过 {cpp_file} (编译失败)")
                overall_success = False
                continue

            # 使用每个测试数据运行
            for data_file in data_files:
                print(f"\n--- 使用测试数据: {data_file.name} ---")

                run_success, output, error, runtime = self.run_cpp_with_input(
                    exe_path, data_file, timeout=60
                )

                # 保存输出
                if output:
                    output_file = exe_path.parent / f"output_{data_file.stem}.txt"
                    self.save_output(output, output_file)

                # 记录结果
                test_result = {
                    "cpp_file": cpp_file,
                    "data_file": data_file,
                    "compile_success": compile_success,
                    "run_success": run_success,
                    "runtime": runtime,
                    "output_length": len(output) if output else 0,
                    "has_error": bool(error),
                }
                self.results.append(test_result)

                if not run_success:
                    overall_success = False

        # 生成测试报告
        self.generate_report()

        return overall_success

    def generate_report(self):
        """生成测试报告"""
        print(f"\n{'='*60}")
        print("测试报告")
        print(f"{'='*60}")

        total_tests = len(self.results)
        successful_tests = sum(1 for r in self.results if r["run_success"])

        print(f"总测试数: {total_tests}")
        print(f"成功测试数: {successful_tests}")
        print(f"失败测试数: {total_tests - successful_tests}")
        print(
            f"成功率: {successful_tests/total_tests*100:.1f}%"
            if total_tests > 0
            else "0%"
        )

        print(f"\n详细结果:")
        for i, result in enumerate(self.results, 1):
            status = "✅ 成功" if result["run_success"] else "❌ 失败"
            print(
                f"  {i}. {result['cpp_file'].name} + {result['data_file'].name}: {status} "
                f"(用时: {result['runtime']:.2f}s, 输出: {result['output_length']} 字符)"
            )

        # 保存报告到文件
        report_file = self.project_root / "test_report.txt"
        with open(report_file, "w", encoding="utf-8") as f:
            f.write(f"C++代码测试报告\n")
            f.write(f"生成时间: {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"总测试数: {total_tests}\n")
            f.write(f"成功测试数: {successful_tests}\n")
            f.write(f"失败测试数: {total_tests - successful_tests}\n")
            f.write(
                f"成功率: {successful_tests/total_tests*100:.1f}%\n\n"
                if total_tests > 0
                else "成功率: 0%\n\n"
            )

            f.write("详细结果:\n")
            for i, result in enumerate(self.results, 1):
                status = "成功" if result["run_success"] else "失败"
                f.write(
                    f"  {i}. {result['cpp_file'].name} + {result['data_file'].name}: {status} "
                    f"(用时: {result['runtime']:.2f}s, 输出: {result['output_length']} 字符)\n"
                )

        print(f"\n测试报告已保存到: {report_file}")


def main():
    """主函数"""
    # 获取项目根目录
    if len(sys.argv) > 1:
        project_root = sys.argv[1]
    else:
        # 默认使用当前脚本所在目录
        project_root = Path(__file__).parent

    print(f"项目根目录: {project_root}")

    # 创建测试器并运行测试
    tester = CPPTester(project_root)
    success = tester.run_full_test()

    if success:
        print(f"\n🎉 所有测试通过!")
        sys.exit(0)
    else:
        print(f"\n⚠️  部分测试失败，请检查代码")
        sys.exit(1)


if __name__ == "__main__":
    main()
