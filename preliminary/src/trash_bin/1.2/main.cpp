#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <limits>

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
    int server_id;     // 服务器id
    int id_in_server;  // NPU id
    long long free_at; // 空闲时间，在哪个时间点之后处于空闲，可以接收请求
};

struct ScheduledRequest
{
    int user_id;          // 用户id
    long long time;       // 请求发送时间
    int server_id;        // 服务器id
    int npu_id_in_server; // NPU id
    int B;                // 批处理大小
};

struct RequestDecision
{
    long long cost;
    int user_idx;
    int npu_idx;
    int B;
    long long finish_time;
};

// --- 全局状态 ---
int N, M;
std::vector<Server> servers;
std::vector<User> users;
std::vector<std::vector<int>> latencies; // latencies[server_idx][user_idx]
int a, b;
std::vector<Npu> npus;

// --- 辅助函数 ---

void read_input() // 读取、解析输入
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

    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < servers[i].g; ++j)
        {
            npus.push_back({servers[i].id, j + 1, 0});
        }
    }
}

RequestDecision find_best_request()
{
    RequestDecision best_decision = {std::numeric_limits<long long>::max(), -1, -1, -1, -1};

    const long long LATE_PENALTY = 100000;
    const int MIGRATION_PENALTY = 50;

    for (int i = 0; i < M; ++i)
    {
        if (users[i].remaining_cnt == 0)
            continue;

        long long send_time = users[i].next_send_time;

        for (size_t j = 0; j < npus.size(); ++j)
        {
            int server_idx = npus[j].server_id - 1;

            std::vector<int> batch_options;
            int max_b_for_user = std::min(users[i].remaining_cnt, servers[server_idx].max_b);
            if (max_b_for_user > 0)
                batch_options.push_back(max_b_for_user);
            if (max_b_for_user > 64)
                batch_options.push_back(64);

            for (int B : batch_options)
            {
                if (B <= 0)
                    continue;

                long long arrival_time = send_time + latencies[server_idx][i];
                long long start_time = std::max(arrival_time, npus[j].free_at);
                long long inference_time = static_cast<long long>(std::ceil(std::sqrt(B) * 1.0 / servers[server_idx].k));
                long long finish_time = start_time + inference_time;

                long long cost = (finish_time - send_time);
                cost += (start_time - arrival_time); // Idle time penalty

                if (users[i].last_server_id != -1 && (npus[j].server_id != users[i].last_server_id || npus[j].id_in_server != users[i].last_npu_id_in_server))
                {
                    cost += MIGRATION_PENALTY;
                }

                cost += std::max(0LL, finish_time - users[i].e) * LATE_PENALTY;

                if (cost < best_decision.cost)
                {
                    best_decision = {cost, i, (int)j, B, finish_time};
                }
            }
        }
    }
    return best_decision;
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
        RequestDecision choice = find_best_request();

        if (choice.user_idx != -1)
        {
            long long send_time = users[choice.user_idx].next_send_time;
            int server_id = npus[choice.npu_idx].server_id;
            int npu_id = npus[choice.npu_idx].id_in_server;
            int server_idx = server_id - 1;

            solution[choice.user_idx].push_back({choice.user_idx + 1, send_time, server_id, npu_id, choice.B});

            users[choice.user_idx].remaining_cnt -= choice.B;
            total_remaining_cnt -= choice.B;

            users[choice.user_idx].last_server_id = server_id;
            users[choice.user_idx].last_npu_id_in_server = npu_id;

            users[choice.user_idx].next_send_time = send_time + latencies[server_idx][choice.user_idx] + 1;

            npus[choice.npu_idx].free_at = choice.finish_time;
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
        if (!solution[i].empty())
        {
            for (size_t j = 0; j < solution[i].size(); ++j)
            {
                std::cout << solution[i][j].time << " "
                          << solution[i][j].server_id << " "
                          << solution[i][j].npu_id_in_server << " "
                          << solution[i][j].B << (j == solution[i].size() - 1 ? "" : " ");
            }
        }
        std::cout << "\n";
    }

    return 0;
}