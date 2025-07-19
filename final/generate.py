#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
华为嵌入式软件大赛复赛 - 可配置数据生成器
支持自定义参数和多种测试场景
"""

import os
import random
from pathlib import Path
import json


class FinalDataGenerator:
    """复赛数据生成器类"""

    def __init__(self, config_file=None):
        """初始化生成器"""
        self.config = (
            self.load_config(config_file) if config_file else self.default_config()
        )

    def default_config(self):
        """默认配置"""
        return {
            "seed": 42,
            "N": {"min": 3, "max": 8, "default": 5},
            "M": {"min": 50, "max": 200, "default": 100},
            "server": {
                "g": {"min": 2, "max": 8, "default": 4},
                "k": {"min": 1, "max": 5, "default": 3},
                "m": {"min": 1200, "max": 1800, "default": 1500},
            },
            "user": {
                "cnt": {"min": 500, "max": 3000, "default": 1500},
                "time_buffer": {"min": 5000, "max": 20000, "default": 10000},
            },
            "latency": {"min": 12, "max": 18, "default": 15},
            "model": {
                "a": {"min": 12, "max": 18, "default": 15},
                "b": {"min": 120, "max": 180, "default": 150},
            },
        }

    def load_config(self, config_file):
        """加载配置文件"""
        try:
            with open(config_file, "r", encoding="utf-8") as f:
                return json.load(f)
        except:
            print(f"⚠️  无法加载配置文件 {config_file}，使用默认配置")
            return self.default_config()

    def generate_servers(self, N):
        """生成服务器配置"""
        servers = []
        server_config = self.config["server"]

        print("生成服务器配置:")
        for i in range(N):
            g = random.randint(server_config["g"]["min"], server_config["g"]["max"])
            k = random.randint(server_config["k"]["min"], server_config["k"]["max"])
            m = random.randint(server_config["m"]["min"], server_config["m"]["max"])

            servers.append({"g": g, "k": k, "m": m})
            print(f"  服务器{i+1}: NPU={g}, 速度系数={k}, 显存={m}MB")

        return servers

    def generate_users(self, M):
        """生成用户配置"""
        users = []
        user_config = self.config["user"]

        print("生成用户配置:")
        for i in range(M):
            # 生成样本数量
            cnt = random.randint(user_config["cnt"]["min"], user_config["cnt"]["max"])

            # 生成时间段
            s = random.randint(0, 20000)
            min_duration = 5 * cnt  # 满足约束 5×cnt ≤ e-s
            buffer_time = random.randint(
                user_config["time_buffer"]["min"], user_config["time_buffer"]["max"]
            )
            e = s + min_duration + buffer_time

            # 确保不超过时间限制
            if e > 60000:
                e = 60000
                s = max(0, e - min_duration - buffer_time // 2)

            users.append({"s": s, "e": e, "cnt": cnt})

            if i < 3:  # 显示前3个用户
                print(f"  用户{i+1}: 时间[{s}, {e}), 样本={cnt}, 时长={e-s}")

        return users

    def generate_latencies(self, N, M):
        """生成通信时延矩阵"""
        latencies = []
        lat_config = self.config["latency"]

        for i in range(N):
            row = []
            for j in range(M):
                if isinstance(lat_config, dict):
                    latency = random.randint(lat_config["min"], lat_config["max"])
                else:
                    latency = random.randint(10, 20)
                row.append(latency)
            latencies.append(row)

        return latencies

    def generate_model_params(self, M):
        """生成用户模型参数"""
        params = []
        model_config = self.config["model"]

        print("生成用户模型参数:")
        for i in range(M):
            a = random.randint(model_config["a"]["min"], model_config["a"]["max"])
            b = random.randint(model_config["b"]["min"], model_config["b"]["max"])
            params.append({"a": a, "b": b})

            if i < 3:  # 显示前3个用户参数
                print(f"  用户{i+1}: a={a}, b={b} (显存需求: {a}×batch+{b})")

        return params

    def generate_data(self, output_file="data.in", scenario="standard"):
        """生成测试数据"""
        print("=" * 70)
        print(f"华为嵌入式软件大赛复赛 - 数据生成器 ({scenario}场景)")
        print("=" * 70)

        # 设置随机种子
        random.seed(self.config["seed"])

        # 确定N和M
        if scenario == "small":
            N, M = 2, 10
        elif scenario == "large":
            N, M = 8, 300
        else:  # standard
            N = random.randint(self.config["N"]["min"], self.config["N"]["max"])
            M = random.randint(self.config["M"]["min"], self.config["M"]["max"])

        print(f"生成参数: N={N}, M={M}")

        # 生成各部分数据
        servers = self.generate_servers(N)
        users = self.generate_users(M)
        latencies = self.generate_latencies(N, M)
        model_params = self.generate_model_params(M)

        # 写入文件
        output_path = Path(output_file)
        with open(output_path, "w", encoding="utf-8") as f:
            # 服务器数量
            f.write(f"{N}\n")

            # 服务器配置
            for server in servers:
                f.write(f"{server['g']} {server['k']} {server['m']}\n")

            # 用户数量
            f.write(f"{M}\n")

            # 用户配置
            for user in users:
                f.write(f"{user['s']} {user['e']} {user['cnt']}\n")

            # 通信时延矩阵
            for row in latencies:
                f.write(" ".join(map(str, row)) + "\n")

            # 用户模型参数（复赛新增）
            for param in model_params:
                f.write(f"{param['a']} {param['b']}\n")

        print(f"\n✅ 测试数据已生成: {output_path.absolute()}")

        # 验证数据
        return self.verify_data(output_path, N, M, servers, users, model_params)

    def verify_data(self, file_path, N, M, servers, users, model_params):
        """验证生成的数据"""
        print("\n📊 数据验证:")

        # 验证约束条件
        constraints_ok = True

        # 验证用户时间约束
        for i, user in enumerate(users):
            if 5 * user["cnt"] > user["e"] - user["s"]:
                print(f"  ❌ 用户{i+1} 时间约束违反")
                constraints_ok = False

        # 验证显存可行性
        min_memory = min(server["m"] for server in servers)
        feasible_count = 0

        for i, param in enumerate(model_params):
            max_batch = (min_memory - param["b"]) // param["a"]
            if max_batch >= 1:
                feasible_count += 1

        feasibility_rate = feasible_count / M

        print(f"  ✓ 时间约束检查: {'通过' if constraints_ok else '失败'}")
        print(f"  ✓ 显存可行性: {feasible_count}/{M} ({feasibility_rate*100:.1f}%)")
        print(f"  ✓ 最小服务器显存: {min_memory}MB")

        success = constraints_ok and feasibility_rate > 0.7
        print(f"\n{'🎉 验证通过!' if success else '❌ 验证失败!'}")

        return success


def main():
    """主函数"""
    generator = FinalDataGenerator()

    print("选择生成场景:")
    print("1. 小规模测试 (2服务器, 10用户)")
    print("2. 标准测试 (随机规模)")
    print("3. 大规模测试 (8服务器, 300用户)")
    print("4. 全部生成")

    choice = input("请选择 (1-4, 默认2): ").strip() or "2"

    try:
        if choice == "1":
            generator.generate_data("small_data.in", "small")
        elif choice == "3":
            generator.generate_data("large_data.in", "large")
        elif choice == "4":
            print("\n生成所有场景的测试数据...")
            generator.generate_data("small_data.in", "small")
            generator.generate_data("data.in", "standard")
            generator.generate_data("large_data.in", "large")
            print("\n🎯 所有测试数据生成完成!")
        else:  # 默认标准测试
            generator.generate_data("data.in", "standard")

    except Exception as e:
        print(f"\n❌ 生成失败: {e}")
        import traceback

        traceback.print_exc()


if __name__ == "__main__":
    main()
