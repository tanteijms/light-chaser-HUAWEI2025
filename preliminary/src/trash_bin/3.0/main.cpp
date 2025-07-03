#pragma GCC optimize("O3", "unroll-loops")
#pragma GCC target("avx2", "bmi", "bmi2", "lzcnt", "popcnt")

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <limits>
#include <set>
#include <functional>

// --- 可调参数 ---
constexpr int MIGRATION_PENALTY = 20; // 任务迁移惩罚, 与1.0/main.cpp保持一致

// --- 数据结构 ---

struct Server
{
    int id;
    int g;     // NPU数量
    int k;     // 推理速度系数
    int m;     // 显存大小
    int max_b; // 该服务器最大batch size
};

struct User
{
    int id;
    int s, e;                  // 用户推理请求时间段[s, e)
    int cnt;                   // 待推理样本数量
    int remaining_cnt;         // 剩余待推理样本数量
    long long next_send_time;  // 下一个请求发送时间
    int last_server_id;        // 上一个请求发送的服务器id
    int last_npu_id_in_server; // 上一个请求发送的NPU id
};

struct Npu
{
    int id_in_server;  // NPU id
    long long free_at; // 空闲时间点

    // 自定义比较运算符，用于std::set排序
    bool operator<(const Npu &other) const
    {
        if (free_at != other.free_at)
        {
            return free_at < other.free_at;
        }
        return id_in_server < other.id_in_server;
    }
};

struct ScheduledRequest
{
    long long time;       // 请求发送时间
    int server_id;        // 服务器id
    int npu_id_in_server; // NPU id
    int B;                // 批处理大小
};

struct CostInfo
{
    long long cost;
    long long finish_time;
    int B;
};

// --- 全局状态 ---
int N, M;
std::vector<Server> servers;
std::vector<User> users;
std::vector<std::vector<int>> latencies; // latencies[server_idx][user_idx]
int a, b;
// 每个服务器的NPU池, 使用set维护，保证有序性
std::vector<std::set<Npu>> server_npus;
// 辅助数组，用于快速查找任意NPU的空闲时间
std::vector<std::vector<long long>> npu_free_times;

// --- 辅助函数 ---

void read_input()
{
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    std::cin >> N;
    servers.resize(N);
    for (int i = 0; i < N; ++i)
    {
        servers[i].id = i + 1;
        std::cin >> servers[i].g >> servers[i].k >> servers[i].m;
    }

    std::cin >> M;
    users.resize(M);
    for (int i = 0; i < M; ++i)
    {
        users[i].id = i + 1;
        std::cin >> users[i].s >> users[i].e >> users[i].cnt;
        users[i].remaining_cnt = users[i].cnt;
        users[i].next_send_time = users[i].s;
        users[i].last_server_id = -1;
        users[i].last_npu_id_in_server = -1;
    }

    latencies.resize(N, std::vector<int>(M));
    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < M; ++j)
        {
            std::cin >> latencies[i][j];
        }
    }

    std::cin >> a >> b;

    for (int i = 0; i < N; ++i)
    {
        if (a == 0)
        {
            servers[i].max_b = 1000;
        }
        else
        {
            servers[i].max_b = std::min(1000, (servers[i].m - b) / a);
        }
    }

    // 初始化NPU数据结构
    server_npus.resize(N);
    npu_free_times.resize(N);
    for (int i = 0; i < N; ++i)
    {
        npu_free_times[i].resize(servers[i].g, 0);
        for (int j = 0; j < servers[i].g; ++j)
        {
            server_npus[i].insert({j + 1, 0});
        }
    }
}

// 成本计算函数
CostInfo calculate_cost(const User &user, int server_idx, const Npu &npu)
{
    const auto &server = servers[server_idx];
    int B = std::min(user.remaining_cnt, server.max_b);
    if (B <= 0)
        return {std::numeric_limits<long long>::max(), -1, -1};

    long long arrival_time = user.next_send_time + latencies[server_idx][user.id - 1];
    long long start_time = std::max(arrival_time, npu.free_at);
    long long inference_time = static_cast<long long>(std::ceil(std::sqrt(B) / server.k));
    long long finish_time = start_time + inference_time;
    long long cost = finish_time;

    if (user.last_server_id != -1 && (server.id != user.last_server_id || npu.id_in_server != user.last_npu_id_in_server))
    {
        cost += MIGRATION_PENALTY;
    }
    return {cost, finish_time, B};
}

// --- 主调度逻辑 ---

int main()
{
    read_input();

    std::vector<std::vector<ScheduledRequest>> solution(M);
    long long total_remaining_cnt = 0;
    for (const auto &user : users)
    {
        total_remaining_cnt += user.cnt;
    }

    while (total_remaining_cnt > 0)
    {
        long long best_cost = std::numeric_limits<long long>::max();
        int best_user_idx = -1;
        int best_server_idx = -1;
        int best_npu_id = -1;
        int best_B = -1;
        long long best_finish_time = -1;

        for (int i = 0; i < M; ++i)
        {
            if (users[i].remaining_cnt == 0)
                continue;

            for (int s_idx = 0; s_idx < N; ++s_idx)
            {
                if (server_npus[s_idx].empty())
                    continue;

                // 候选1：此服务器上最空闲的NPU
                const Npu &freest_npu = *server_npus[s_idx].begin();
                CostInfo info1 = calculate_cost(users[i], s_idx, freest_npu);
                if (info1.cost < best_cost)
                {
                    best_cost = info1.cost;
                    best_user_idx = i;
                    best_server_idx = s_idx;
                    best_npu_id = freest_npu.id_in_server;
                    best_B = info1.B;
                    best_finish_time = info1.finish_time;
                }

                // 候选2：用户上次使用的NPU（如果在此服务器上）
                if (users[i].last_server_id - 1 == s_idx)
                {
                    int last_npu_id = users[i].last_npu_id_in_server;
                    if (last_npu_id != freest_npu.id_in_server)
                    {
                        long long last_npu_free_at = npu_free_times[s_idx][last_npu_id - 1];
                        Npu last_npu = {last_npu_id, last_npu_free_at};
                        CostInfo info2 = calculate_cost(users[i], s_idx, last_npu);
                        if (info2.cost < best_cost)
                        {
                            best_cost = info2.cost;
                            best_user_idx = i;
                            best_server_idx = s_idx;
                            best_npu_id = last_npu_id;
                            best_B = info2.B;
                            best_finish_time = info2.finish_time;
                        }
                    }
                }
            }
        }

        if (best_user_idx != -1)
        {
            auto &user = users[best_user_idx];
            auto &server = servers[best_server_idx];

            // 更新状态
            solution[best_user_idx].push_back({user.next_send_time, server.id, best_npu_id, best_B});

            user.remaining_cnt -= best_B;
            total_remaining_cnt -= best_B;
            user.last_server_id = server.id;
            user.last_npu_id_in_server = best_npu_id;
            user.next_send_time = user.next_send_time + latencies[best_server_idx][best_user_idx] + 1;

            // 更新NPU数据结构
            long long old_free_at = npu_free_times[best_server_idx][best_npu_id - 1];
            server_npus[best_server_idx].erase({best_npu_id, old_free_at});

            npu_free_times[best_server_idx][best_npu_id - 1] = best_finish_time;
            server_npus[best_server_idx].insert({best_npu_id, best_finish_time});
        }
        else
        {
            break;
        }
    }

    // --- 输出 ---
    for (int i = 0; i < M; ++i)
    {
        std::cout << solution[i].size() << "\n";
        bool first = true;
        for (const auto &req : solution[i])
        {
            if (!first)
            {
                std::cout << " ";
            }
            std::cout << req.time << " " << req.server_id << " " << req.npu_id_in_server << " " << req.B;
            first = false;
        }
        std::cout << "\n";
    }

    return 0;
}