#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_map>

// --- 数据结构 ---

struct Server
{
    int id;
    int g;                          // NPU数量
    int k;                          // 推理速度系数
    int m;                          // 显存大小
    std::vector<int> user_max_b;    // 每个用户在该服务器上的最大batch size [user_idx]
    std::vector<double> efficiency; // 预计算的不同批处理大小的效率 [batch_size]
    int optimal_b_overall;          // 该服务器全局最优的batch size
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
    int a, b;                  // 用户特定的显存参数: Memory = a_i * batchsize + b_i
};

struct Npu
{
    int server_id;              // 服务器id
    int id_in_server;           // NPU id
    long long free_at;          // 空闲时间
    long long utilization_time; // NPU累计工作时长，用于负载均衡
};

struct ScheduledRequest
{
    int user_id;          // 用户id
    long long time;       // 请求发送时间
    int server_id;        // 服务器id
    int npu_id_in_server; // NPU id
    int B;                // 批处理大小
};

// --- 全局状态 ---
int N, M;
std::vector<Server> servers;
std::vector<User> users;
std::vector<std::vector<int>> latencies; // latencies[server_idx][user_idx]
std::vector<Npu> npus;
const int MAX_BATCH_SIZE = 1000; // 最大批处理大小
// 成本函数中的权重系数，用于调优
const long long DEADLINE_PENALTY_WEIGHT = 1000;
const int MIGRATION_PENALTY = 20;
const int LOAD_BALANCE_WEIGHT = 1;

// --- 辅助函数 ---

// 计算推理耗时
double calculate_inference_time(int B, int k)
{
    if (B <= 0)
        return 0;
    return std::ceil(B / (k * std::sqrt(B)));
}

// 计算推理效率
double calculate_efficiency(int B, int k)
{
    if (B <= 0)
        return 0;
    double inference_time = calculate_inference_time(B, k);
    if (inference_time < 1)
        return B;
    return B / inference_time;
}

// 计算用户在服务器上的最大batch
int calculate_max_batch(int server_m, int user_a, int user_b)
{
    if (user_a == 0)
        return MAX_BATCH_SIZE;
    return std::min(MAX_BATCH_SIZE, (server_m - user_b) / user_a);
}

// 预计算服务器效率和最佳Batch
void precalculate_server_stats(Server &server)
{
    server.efficiency.resize(MAX_BATCH_SIZE + 1, 0.0);
    double best_efficiency = 0;
    server.optimal_b_overall = 1;

    for (int b = 1; b <= MAX_BATCH_SIZE; ++b)
    {
        server.efficiency[b] = calculate_efficiency(b, server.k);
        if (server.efficiency[b] > best_efficiency)
        {
            best_efficiency = server.efficiency[b];
            server.optimal_b_overall = b;
        }
    }
}

// 根据剩余样本和服务器能力，找到最佳Batch
int find_optimal_batch(const Server &server, int max_batch_for_user, int remaining_samples, int min_b_required)
{
    int search_limit = std::min(remaining_samples, max_batch_for_user);
    if (search_limit < min_b_required)
    {
        // 无法在满足最小B要求和显存限制的前提下进行调度
        return 0;
    }

    // 在 [min_b_required, search_limit] 范围内搜索最优B
    double best_efficiency = -1.0;
    int best_b = 0;
    for (int b = min_b_required; b <= search_limit; ++b)
    {
        if (server.efficiency[b] > best_efficiency)
        {
            best_efficiency = server.efficiency[b];
            best_b = b;
        }
    }
    return best_b;
}

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
        precalculate_server_stats(servers[i]);
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

    for (int i = 0; i < M; ++i)
    {
        std::cin >> users[i].a >> users[i].b;
    }

    for (int i = 0; i < N; ++i)
    {
        servers[i].user_max_b.resize(M);
        for (int j = 0; j < M; ++j)
        {
            servers[i].user_max_b[j] = calculate_max_batch(servers[i].m, users[j].a, users[j].b);
        }
    }

    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < servers[i].g; ++j)
        {
            npus.push_back({servers[i].id, j + 1, 0, 0});
        }
    }
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

    long long current_time = 0;

    while (total_remaining_cnt > 0)
    {
        long long current_time = std::numeric_limits<long long>::max();
        for (int i = 0; i < M; ++i)
        {
            if (users[i].remaining_cnt > 0)
            {
                current_time = std::min(current_time, users[i].next_send_time);
            }
        }
        if (current_time == std::numeric_limits<long long>::max())
        {
            break; // 所有用户处理完毕
        }

        // --- 在current_time进行调度决策 ---
        long long best_cost = std::numeric_limits<long long>::max();
        int best_user_idx = -1;
        int best_npu_idx = -1;
        int best_B = -1;
        long long best_finish_time = -1;

        // 遍历所有可以在 current_time 或之前发送请求的用户
        for (int i = 0; i < M; ++i)
        {
            if (users[i].remaining_cnt == 0 || users[i].next_send_time > current_time)
            {
                continue;
            }

            // 计算满足T_i <= 300约束的最小B
            int requests_so_far = solution[i].size();
            int remaining_requests_allowed = 300 - requests_so_far;
            int min_b_required = 1;
            if (remaining_requests_allowed > 0)
            {
                min_b_required = static_cast<int>(std::ceil(static_cast<double>(users[i].remaining_cnt) / remaining_requests_allowed));
            }
            else if (users[i].remaining_cnt > 0)
            {
                // 请求次数已达上限，必须一次性发完所有剩余样本
                min_b_required = users[i].remaining_cnt;
            }

            // 遍历所有NPU，为该用户寻找最佳调度方案
            for (size_t j = 0; j < npus.size(); ++j)
            {
                int server_idx = npus[j].server_id - 1;
                int max_b = servers[server_idx].user_max_b[i];
                if (max_b <= 0)
                    continue;

                int optimal_B = find_optimal_batch(servers[server_idx], max_b, users[i].remaining_cnt, min_b_required);
                if (optimal_B <= 0)
                    continue;

                long long send_time = users[i].next_send_time;
                long long arrival_time = send_time + latencies[server_idx][i];
                long long start_time = std::max(arrival_time, npus[j].free_at);
                long long inference_time = static_cast<long long>(calculate_inference_time(optimal_B, servers[server_idx].k));
                long long finish_time = start_time + inference_time;

                // 优化后的成本函数
                long long time_over_deadline = std::max(0LL, finish_time - users[i].e);
                long long cost = finish_time + time_over_deadline * DEADLINE_PENALTY_WEIGHT;

                // 迁移惩罚
                if (users[i].last_server_id != -1 &&
                    (npus[j].server_id != users[i].last_server_id ||
                     npus[j].id_in_server != users[i].last_npu_id_in_server))
                {
                    cost += MIGRATION_PENALTY;
                }

                // 负载均衡惩罚 (基于NPU累计工作时间)
                cost += npus[j].utilization_time * LOAD_BALANCE_WEIGHT;

                if (cost < best_cost)
                {
                    best_cost = cost;
                    best_user_idx = i;
                    best_npu_idx = j;
                    best_B = optimal_B;
                    best_finish_time = finish_time;
                }
            }
        }

        // --- 执行最优调度 ---
        if (best_user_idx != -1)
        {
            int user_id = users[best_user_idx].id;
            long long send_time = users[best_user_idx].next_send_time;
            int server_id = npus[best_npu_idx].server_id;
            int npu_id = npus[best_npu_idx].id_in_server;

            solution[best_user_idx].push_back({user_id, send_time, server_id, npu_id, best_B});

            // 更新状态
            users[best_user_idx].remaining_cnt -= best_B;
            total_remaining_cnt -= best_B;

            users[best_user_idx].last_server_id = server_id;
            users[best_user_idx].last_npu_id_in_server = npu_id;

            int server_idx = server_id - 1;
            // 题目规则: 用户可在第 x+latency+1 毫秒发送下一个请求
            users[best_user_idx].next_send_time = send_time + latencies[server_idx][best_user_idx] + 1;

            long long inference_time = best_finish_time - std::max(send_time + latencies[server_idx][best_user_idx], npus[best_npu_idx].free_at);
            npus[best_npu_idx].free_at = best_finish_time;
            npus[best_npu_idx].utilization_time += inference_time;
        }
        else
        {
            // --- 死循环处理 ---
            // 在当前时间点，所有就绪的用户都无法找到任何一个NPU进行有效调度
            // (通常因为min_b_required过大，所有服务器均不满足)
            // 必须强制推进时间，否则会无限循环
            long long next_possible_event_time = std::numeric_limits<long long>::max();

            // 找到下一个NPU释放的时刻
            for (const auto &npu : npus)
            {
                if (npu.free_at > current_time)
                {
                    next_possible_event_time = std::min(next_possible_event_time, npu.free_at);
                }
            }

            if (next_possible_event_time == std::numeric_limits<long long>::max())
            {
                // 如果所有NPU都已空闲，但依然无法调度，说明存在根本性的逻辑冲突，无法解决
                break;
            }

            // 将一个被卡住的用户的时间推进到下一可能时刻，以打破僵局
            bool advanced = false;
            for (int i = 0; i < M; ++i)
            {
                if (users[i].remaining_cnt > 0 && users[i].next_send_time <= current_time)
                {
                    users[i].next_send_time = next_possible_event_time;
                    advanced = true;
                    break;
                }
            }
            if (!advanced)
            {
                // 如果没有找到任何一个卡住的用户（理论上不应该），则直接退出
                break;
            }
        }
    }

    // --- 输出 ---
    for (int i = 0; i < M; ++i)
    {
        std::cout << solution[i].size() << "\n";
        for (size_t j = 0; j < solution[i].size(); ++j)
        {
            std::cout << solution[i][j].time << " "
                      << solution[i][j].server_id << " "
                      << solution[i][j].npu_id_in_server << " "
                      << solution[i][j].B;
            if (j < solution[i].size() - 1)
            {
                std::cout << " ";
            }
        }
        std::cout << "\n";
    }

    return 0;
}