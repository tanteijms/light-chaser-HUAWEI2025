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
    int g;                                            // NPU数量
    int k;                                            // 推理速度系数
    int m;                                            // 显存大小
    int max_b;                                        // 该服务器最大batch size
    std::unordered_map<int, double> batch_efficiency; // 缓存不同batch size的效率
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
    double urgency;            // 紧急程度 = remaining_cnt / (e - current_time)
};

struct Npu
{
    int server_id;     // 服务器id
    int id_in_server;  // NPU id
    long long free_at; // 空闲时间，在哪个时间点之后处于空闲，可以接收请求
    int utilization;   // 使用率统计，用于负载均衡
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
int a, b;
std::vector<Npu> npus;
const int MIGRATION_PENALTY = 20; // 20是经验最佳值

// --- 辅助函数 ---

// 计算批处理大小为B时的推理效率(每个时间单位处理的样本数)
double calculate_efficiency(int B, int k)
{
    if (B <= 0)
        return 0;
    double inference_time = std::ceil(std::sqrt(B) / k);
    return B / inference_time;
}

// 为给定服务器计算最佳批处理大小
int calculate_optimal_batch(const Server &server, int max_samples)
{
    int max_b = std::min(max_samples, server.max_b);
    if (max_b <= 1)
        return max_b;

    // 为了避免重复计算，我们使用缓存
    if (server.batch_efficiency.count(max_b) > 0)
    {
        return max_b;
    }

    // 尝试不同的批处理大小，找到效率最高的
    double best_efficiency = 0;
    int best_b = 1;

    for (int b = 1; b <= max_b; b++)
    {
        double efficiency = calculate_efficiency(b, server.k);
        if (efficiency > best_efficiency)
        {
            best_efficiency = efficiency;
            best_b = b;
        }
    }

    return best_b;
}

void read_input() // 读取、解析输入
{
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL); // 解除C++流与C标准I/O的同步，大幅提升读取大量数据时的速度

    std::cin >> N;     // 读取服务器数量
    servers.resize(N); // 初始化服务器数组
    for (int i = 0; i < N; ++i)
    {
        servers[i].id = i + 1;                                    // 服务器id
        std::cin >> servers[i].g >> servers[i].k >> servers[i].m; // 读取服务器参数配置
    }

    std::cin >> M;   // 读取用户数量
    users.resize(M); // 初始化用户数组
    for (int i = 0; i < M; ++i)
    {
        users[i].id = i + 1;                                  // 用户id
        std::cin >> users[i].s >> users[i].e >> users[i].cnt; // 读取用户参数配置
        users[i].remaining_cnt = users[i].cnt;                // 初始化剩余待推理样本数量
        users[i].next_send_time = users[i].s;                 // 初始化下一个请求发送时间
        users[i].last_server_id = -1;                         // 初始化上一个请求发送的服务器id
        users[i].last_npu_id_in_server = -1;                  // 初始化上一个请求发送的NPU id
        users[i].urgency = 0;                                 // 初始化紧急程度
    }

    latencies.resize(N, std::vector<int>(M)); // 初始化通信时延矩阵
    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < M; ++j)
        {
            std::cin >> latencies[i][j]; // 读取通信时延
        }
    }

    std::cin >> a >> b; // 读取显存和batchsize的关系

    // 预计算每个服务器的最大batch size
    for (int i = 0; i < N; ++i)
    {
        if (a == 0)
        {
            servers[i].max_b = 1000;
        }
        else
        {
            servers[i].max_b = std::min(1000, (servers[i].m - b) / a); // 计算后直接存入Server结构体
        }
    }

    // 初始化NPUs
    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < servers[i].g; ++j)
        {
            npus.push_back({servers[i].id, j + 1, 0, 0});
        }
    }
}

// 更新用户的紧急度
void update_user_urgency(long long current_time)
{
    for (int i = 0; i < M; ++i)
    {
        if (users[i].remaining_cnt <= 0)
            continue;
        long long remaining_time = std::max(1LL, users[i].e - current_time);
        users[i].urgency = static_cast<double>(users[i].remaining_cnt) / remaining_time;
    }
}

// --- 主调度逻辑 ---

int main() // 主函数
{
    read_input();

    std::vector<std::vector<ScheduledRequest>> solution(M);
    long long total_remaining_cnt = 0;
    for (const auto &user : users)
    {
        total_remaining_cnt += user.cnt; // 主循环终止条件
    }

    // 动态调度
    while (total_remaining_cnt > 0)
    {
        long long best_cost = std::numeric_limits<long long>::max();
        int best_user_idx = -1;
        int best_npu_idx = -1;
        int best_B = -1;
        long long best_finish_time = -1;
        long long current_time = std::numeric_limits<long long>::max();

        // 找到当前最早的可能调度时间
        for (int i = 0; i < M; ++i)
        {
            if (users[i].remaining_cnt > 0)
            {
                current_time = std::min(current_time, users[i].next_send_time);
            }
        }

        // 更新用户的紧急度
        update_user_urgency(current_time);

        // 对用户按紧急度排序，紧急程度高的先处理
        std::vector<int> user_indices(M);
        std::iota(user_indices.begin(), user_indices.end(), 0);
        std::sort(user_indices.begin(), user_indices.end(), [](int a, int b)
                  { return users[a].urgency > users[b].urgency; });

        // 找到下一个要调度的请求
        for (int idx : user_indices)
        {
            if (users[idx].remaining_cnt == 0 || users[idx].next_send_time > current_time)
            {
                continue;
            }

            long long send_time = users[idx].next_send_time;

            // 按NPU空闲时间排序，优先选择更早空闲的NPU
            std::vector<size_t> npu_indices(npus.size());
            std::iota(npu_indices.begin(), npu_indices.end(), 0);
            std::sort(npu_indices.begin(), npu_indices.end(), [](size_t a, size_t b)
                      { return npus[a].free_at < npus[b].free_at; });

            for (size_t npu_idx : npu_indices)
            {
                int server_idx = npus[npu_idx].server_id - 1;

                // 计算最优批处理大小
                int optimal_B = calculate_optimal_batch(servers[server_idx], users[idx].remaining_cnt);
                if (optimal_B <= 0)
                    continue;

                // 评估该调度的成本
                long long arrival_time = send_time + latencies[server_idx][idx];
                long long start_time = std::max(arrival_time, npus[npu_idx].free_at);
                long long inference_time = static_cast<long long>(std::ceil(std::sqrt(optimal_B) / servers[server_idx].k));
                long long finish_time = start_time + inference_time;

                // 成本函数：完成时间 + 紧急度权重 + 迁移惩罚
                long long cost = finish_time;

                // 如果用户接近截止时间，增加优先级
                double deadline_factor = 1.0;
                if (finish_time > users[idx].e)
                {
                    deadline_factor = 2.0; // 超过截止时间，成本加倍
                }
                else
                {
                    long long time_to_deadline = users[idx].e - finish_time;
                    if (time_to_deadline < 5000)
                    {
                        deadline_factor = 1.5; // 接近截止时间，增加优先级
                    }
                }
                cost = static_cast<long long>(cost * deadline_factor);

                // 迁移惩罚
                if (users[idx].last_server_id != -1 &&
                    (npus[npu_idx].server_id != users[idx].last_server_id ||
                     npus[npu_idx].id_in_server != users[idx].last_npu_id_in_server))
                {
                    cost += MIGRATION_PENALTY;
                }

                // 考虑NPU的负载均衡
                cost += npus[npu_idx].utilization * 5; // 使用率越高，成本越高

                if (cost < best_cost)
                {
                    best_cost = cost;
                    best_user_idx = idx;
                    best_npu_idx = npu_idx;
                    best_B = optimal_B;
                    best_finish_time = finish_time;
                }
            }
        }

        if (best_user_idx != -1)
        {
            // 调度最优请求
            long long send_time = users[best_user_idx].next_send_time;
            int server_id = npus[best_npu_idx].server_id;
            int npu_id = npus[best_npu_idx].id_in_server;

            solution[best_user_idx].push_back({best_user_idx + 1, send_time, server_id, npu_id, best_B});

            // 更新状态
            users[best_user_idx].remaining_cnt -= best_B;
            total_remaining_cnt -= best_B;

            users[best_user_idx].last_server_id = server_id;
            users[best_user_idx].last_npu_id_in_server = npu_id;

            int server_idx = server_id - 1;
            users[best_user_idx].next_send_time = send_time + latencies[server_idx][best_user_idx] + 1;

            npus[best_npu_idx].free_at = best_finish_time;
            npus[best_npu_idx].utilization++; // 增加使用计数
        }
        else
        {
            // 如果所有用户当前时间点都无法调度，找到最早的下一个可能调度时间
            long long next_time = std::numeric_limits<long long>::max();
            for (int i = 0; i < M; ++i)
            {
                if (users[i].remaining_cnt > 0)
                {
                    next_time = std::min(next_time, users[i].next_send_time);
                }
            }

            for (size_t j = 0; j < npus.size(); ++j)
            {
                next_time = std::min(next_time, npus[j].free_at);
            }

            // 如果没有找到下一个时间点，说明出现了问题，退出循环
            if (next_time == std::numeric_limits<long long>::max())
            {
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
            std::cout << (j == solution[i].size() - 1 ? "" : " ");
        }
        std::cout << "\n";
    }

    return 0;
}