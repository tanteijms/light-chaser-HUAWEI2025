#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_map>
#include <random>
#include <utility>

// 调试函数
// void print_matrix(const std::vector<std::vector<int>> &matrix)
// {
//     for (const auto &row : matrix)
//     {
//         for (const auto &val : row)
//         {
//             std::cout << val << " ";
//         }
//         std::cout << "\n";
//     }
// }

// --- 数据结构 ---

// 二维数组存储cost
struct CostInfo
{
    long long cost = std::numeric_limits<long long>::max();
    int optimal_B = -1;
    long long finish_time = -1;

    // 重载输出运算符，只输出cost
    friend std::ostream &operator<<(std::ostream &os, const CostInfo &info)
    {
        os << info.cost;
        return os;
    }
};

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
    double urgency;            // 紧急度 = remaining_cnt / (deadline - current_time)
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
const int MIGRATION_PENALTY = 70;
const int LOAD_BALANCE_WEIGHT = 1;
const double SOFTMAX_TEMPERATURE = 0.0000001; // softmax温度参数，控制概率分布的锐度
const int TOP_K = 5;                          // top-k策略中的k值，从最优的k个选项中随机选择

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

// 智能Batch选择 - 考虑时间窗口和效率平衡
int find_optimal_batch_smart(const Server &server, int max_batch_for_user, int remaining_samples,
                             int min_b_required, long long remaining_time, double urgency)
{
    int search_limit = std::min(remaining_samples, max_batch_for_user);
    if (search_limit < min_b_required)
    {
        return 0;
    }

    // 如果时间非常紧张，优先选择较大的batch
    if (remaining_time < 5000 || urgency > 1.0)
    {
        // 紧急情况下，选择接近最大允许的batch
        int urgent_batch = std::min(search_limit, static_cast<int>(remaining_samples * 0.8));
        if (urgent_batch >= min_b_required)
        {
            return urgent_batch;
        }
    }

    // 正常情况下，寻找效率最优的batch
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

// 更新用户紧急度
void update_user_urgency(long long current_time)
{
    for (int i = 0; i < M; ++i)
    {
        if (users[i].remaining_cnt <= 0)
        {
            users[i].urgency = 0;
            continue;
        }
        long long remaining_time = std::max(1LL, users[i].e - current_time);
        users[i].urgency = static_cast<double>(users[i].remaining_cnt) / remaining_time;
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

    // 修复变量遮蔽问题 - 移除重复声明
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

        // 更新用户紧急度
        update_user_urgency(current_time);

        // 按紧急度对用户排序，优先处理紧急的用户
        std::vector<int> user_indices;
        for (int i = 0; i < M; ++i)
        {
            if (users[i].remaining_cnt > 0 && users[i].next_send_time <= current_time)
            {
                user_indices.push_back(i);
            }
        }

        std::sort(user_indices.begin(), user_indices.end(), [](int a, int b)
                  {
                      return users[a].urgency > users[b].urgency; // 紧急度高的优先
                  });

        // --- 在current_time进行调度决策 ---
        long long best_cost = std::numeric_limits<long long>::max();
        int best_user_idx = -1;
        int best_npu_idx = -1;
        int best_B = -1;
        long long best_finish_time = -1;

        std::vector<std::vector<CostInfo>>
            cost_matrix(M, std::vector<CostInfo>(npus.size()));

        // 遍历按紧急度排序的用户
        for (int i : user_indices)
        {

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
                {
                    cost_matrix[i][j].cost = std::numeric_limits<long long>::max();
                    continue; // 无法满足请求
                }

                long long send_time = users[i].next_send_time;
                long long arrival_time = send_time + latencies[server_idx][i];
                long long start_time = std::max(arrival_time, npus[j].free_at);
                long long inference_time = static_cast<long long>(calculate_inference_time(optimal_B, servers[server_idx].k));
                long long finish_time = start_time + inference_time;
                cost_matrix[i][j].finish_time = finish_time; // 记录完成时间

                // 改进的成本函数 - 考虑更多因素
                long long time_over_deadline = std::max(0LL, finish_time - users[i].e);
                long long cost = finish_time;

                // 1. 截止时间惩罚 (非线性)
                if (time_over_deadline > 0)
                {
                    cost += time_over_deadline * time_over_deadline / 1000 + time_over_deadline * DEADLINE_PENALTY_WEIGHT;
                }

                // 2. 紧急度因子
                long long remaining_time = std::max(1LL, users[i].e - current_time);
                if (remaining_time < 10000)
                { // 时间紧张时
                    cost = static_cast<long long>(cost * (1.0 + users[i].urgency * 0.1));
                }

                // 3. 效率奖励 - 选择高效batch的奖励
                double efficiency_bonus = servers[server_idx].efficiency[optimal_B] * 10;
                cost -= static_cast<long long>(efficiency_bonus);

                // 4. 迁移惩罚 (渐进式)
                if (users[i].last_server_id != -1 &&
                    (npus[j].server_id != users[i].last_server_id ||
                     npus[j].id_in_server != users[i].last_npu_id_in_server))
                {
                    // 根据已发送请求数量调整迁移惩罚
                    int sent_requests = solution[i].size();
                    int migration_penalty = MIGRATION_PENALTY * (1 + sent_requests / 10);
                    cost += migration_penalty;
                }

                // 5. 负载均衡 (考虑相对负载)
                double avg_utilization = 0;
                for (const auto &npu : npus)
                {
                    avg_utilization += npu.utilization_time;
                }
                avg_utilization /= npus.size();

                double relative_load = npus[j].utilization_time - avg_utilization;
                cost += static_cast<long long>(relative_load * LOAD_BALANCE_WEIGHT);

                // 6. 服务器匹配度奖励
                if (npus[j].server_id == users[i].last_server_id)
                {
                    cost /= 50; // 继续使用同一服务器的奖励
                }

                // 记录成本和最优B
                cost_matrix[i][j].cost = cost;
                cost_matrix[i][j].optimal_B = optimal_B;

                // if (cost < best_cost)
                // {
                //     best_cost = cost;
                //     best_user_idx = i;
                //     best_npu_idx = j;
                //     best_B = optimal_B;
                //     best_finish_time = finish_time;
                // }
            }
        }

        // printf("Current time: %lld\n", current_time);
        // // 打印cost_matrix的cost值用于调试
        // for (const auto &row : cost_matrix)
        // {
        //     for (const auto &info : row)
        //     {
        //         if (info.cost == std::numeric_limits<long long>::max())
        //             std::cout << "INF ";
        //         else
        //             std::cout << info.cost << " ";
        //     }
        //     std::cout << "\n";
        // }

        // 按概率分布采样选取best_user_idx和best_npu_idx
        best_cost = std::numeric_limits<long long>::max();

        // 收集所有有效的(user_idx, npu_idx)对及其cost
        std::vector<std::tuple<int, int, long long>> valid_options; // (user_idx, npu_idx, cost)

        for (int i : user_indices)
        {
            for (size_t j = 0; j < npus.size(); ++j)
            {
                if (cost_matrix[i][j].cost < std::numeric_limits<long long>::max())
                {
                    valid_options.push_back(std::make_tuple(i, j, cost_matrix[i][j].cost));
                }
            }
        }

        if (!valid_options.empty())
        {
            // 方法：Top-K策略，从cost最小的k个选项中随机选择

            // 1. 按cost从小到大排序（cost越小越好）
            std::vector<size_t> indices(valid_options.size());
            std::iota(indices.begin(), indices.end(), 0);

            std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b)
                      { return std::get<2>(valid_options[a]) < std::get<2>(valid_options[b]); });

            // 2. 确定实际的k值（不超过可用选项数量）
            int actual_k = std::min(TOP_K, static_cast<int>(valid_options.size()));

            // 3. 从前k个最优选项中随机选择一个
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<> dist(0, actual_k - 1);

            int selected_rank = dist(gen);             // 在前k个中随机选择
            int selected_idx = indices[selected_rank]; // 获取原始索引

            best_user_idx = std::get<0>(valid_options[selected_idx]);
            best_npu_idx = std::get<1>(valid_options[selected_idx]);
            best_B = cost_matrix[best_user_idx][best_npu_idx].optimal_B;
            best_finish_time = cost_matrix[best_user_idx][best_npu_idx].finish_time;
            best_cost = cost_matrix[best_user_idx][best_npu_idx].cost;
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

    std::cout.flush(); // 强制清空输出缓存

    return 0;
}