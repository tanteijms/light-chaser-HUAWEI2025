#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
华为嵌入式软件大赛复赛 - 快速评分脚本
简化版本，快速计算得分
"""

import math
import sys
import os
from pathlib import Path

os.chdir(Path(__file__).parent)


def quick_score(input_file="data.in", output_file="output.out"):
    """快速评分函数"""

    # 读取输入数据
    try:
        with open(input_file, "r") as f:
            lines = f.readlines()

        line_idx = 0
        N = int(lines[line_idx].strip())
        line_idx += 1

        servers = []
        for i in range(N):
            g, k, m = map(int, lines[line_idx].strip().split())
            servers.append({"g": g, "k": k, "m": m})
            line_idx += 1

        M = int(lines[line_idx].strip())
        line_idx += 1

        users = []
        for i in range(M):
            s, e, cnt = map(int, lines[line_idx].strip().split())
            users.append({"s": s, "e": e, "cnt": cnt})
            line_idx += 1

        # 跳过通信时延矩阵
        line_idx += N

        user_params = []
        for i in range(M):
            a, b = map(int, lines[line_idx].strip().split())
            user_params.append({"a": a, "b": b})
            line_idx += 1

    except Exception as e:
        print(f"❌ 读取输入失败: {e}")
        return 0

    # 读取输出数据
    try:
        with open(output_file, "r") as f:
            output_lines = f.readlines()

        if len(output_lines) != 2 * M:
            print(f"❌ 输出行数错误: 期望{2*M}, 实际{len(output_lines)}")
            return 0

        user_schedules = []
        for i in range(M):
            T_i = int(output_lines[2 * i].strip())
            request_data = list(map(int, output_lines[2 * i + 1].strip().split()))

            if len(request_data) != 4 * T_i:
                print(f"❌ 用户{i+1}数据长度错误")
                return 0

            requests = []
            for j in range(T_i):
                time = request_data[4 * j]
                server = request_data[4 * j + 1]
                npu = request_data[4 * j + 2]
                batch = request_data[4 * j + 3]
                requests.append(
                    {"time": time, "server": server, "npu": npu, "batch": batch}
                )

            user_schedules.append(requests)

    except Exception as e:
        print(f"❌ 读取输出失败: {e}")
        return 0

    # 简化评分（基于请求完成时间的估算）
    total_score = 0.0
    K = 0

    print("快速评分结果:")
    print(
        f"{'用户':>4} {'要求结束':>10} {'估计完成':>10} {'是否超时':>8} {'迁移':>6} {'得分':>10}"
    )
    print("-" * 60)

    for i in range(M):
        user = users[i]
        user_param = user_params[i]
        requests = user_schedules[i]

        # 简化计算：估算最后一个请求的完成时间
        if not requests:
            continue

        last_request = requests[-1]
        server_idx = last_request["server"] - 1

        # 估算推理时间
        inference_time = math.ceil(
            math.sqrt(last_request["batch"]) / servers[server_idx]["k"]
        )
        estimated_end = last_request["time"] + 20 + inference_time  # 20是通信时延估算

        # 计算迁移次数（简化版）
        migrations = 0
        last_npu = None
        for req in requests:
            current_npu = (req["server"], req["npu"])
            if last_npu is not None and last_npu != current_npu:
                migrations += 1
            last_npu = current_npu

        # 计算得分
        is_overtime = estimated_end > user["e"]
        if is_overtime:
            K += 1

        delay_ratio = max(0, (estimated_end - user["e"]) / (user["e"] - user["s"]))
        h_delay = math.pow(2, -delay_ratio / 100)
        p_migration = math.pow(2, -migrations / 200)
        individual_score = h_delay * p_migration * 10000
        total_score += individual_score

        print(
            f"{i+1:>4} {user['e']:>10} {estimated_end:>10} {'是' if is_overtime else '否':>8} "
            f"{migrations:>6} {individual_score:>10.1f}"
        )

    # 全局惩罚
    h_global = math.pow(2, -K / 100)
    final_score = h_global * total_score

    print("-" * 60)
    print(f"超时用户: {K}, 全局惩罚: {h_global:.4f}")
    print(f"🏆 估算得分: {final_score:.1f}")

    return final_score


if __name__ == "__main__":
    input_file = sys.argv[1] if len(sys.argv) > 1 else "data.in"
    output_file = sys.argv[2] if len(sys.argv) > 2 else "output.out"

    print("华为嵌入式软件大赛复赛 - 快速评分")
    print(f"输入: {input_file}, 输出: {output_file}")

    if not Path(input_file).exists():
        print(f"❌ 文件不存在: {input_file}")
    elif not Path(output_file).exists():
        print(f"❌ 文件不存在: {output_file}")
    else:
        score = quick_score(input_file, output_file)
