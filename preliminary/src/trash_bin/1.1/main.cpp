#pragma GCC optimize("O3", "unroll-loops")
#pragma GCC target("avx2", "bmi", "bmi2", "lzcnt", "popcnt")

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <limits>
#include <queue>
#include <functional>

// --- 可调参数 ---
constexpr int MIGRATION_PENALTY = 20;       // 任务迁移惩罚
constexpr int K_BEST_SERVERS = 10;          // 每个用户考虑的延迟最低的服务器数量
constexpr int L_BEST_AVAILABLE_SERVERS = 5; // 每一轮考虑的全局最空闲的服务器数量

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

// NPU状态，用于优先队列
struct Npu
{
    int id_in_server;  // NPU id
    long long free_at; // 空闲时间点

    // 自定义比较运算符，用于最小堆
    bool operator>(const Npu &other) const
    {
        return free_at > other.free_at;
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
    long long start_time;
    long long finish_time;
    int B;
};

// 用于事件驱动的用户队列
struct UserEvent
{
    long long time; // user's next_send_time
    int user_idx;

    bool operator>(const UserEvent &other) const
    {
        if (time != other.time)
            return time > other.time;
        return user_idx > other.user_idx;
    }
};

// --- 全局状态 ---
int N, M;
std::vector<Server> servers;
std::vector<User> users;
std::vector<std::vector<int>> latencies; // latencies[server_idx][user_idx]
int a, b;
// 每个服务器的NPU池，使用最小优先队列维护，按free_at排序
std::vector<std::priority_queue<Npu, std::vector<Npu>, std::greater<Npu>>> server_npus;
std::vector<std::vector<int>> best_servers_for_user; // 用户的K个最佳服务器索引

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

    // 初始化NPU优先队列
    server_npus.resize(N);
    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < servers[i].g; ++j)
        {
            server_npus[i].push({j + 1, 0});
        }
    }

    // 预计算每个用户的最佳服务器
    best_servers_for_user.resize(M);
    for (int i = 0; i < M; ++i)
    {
        std::vector<std::pair<int, int>> server_latencies;
        for (int j = 0; j < N; ++j)
        {
            server_latencies.push_back({latencies[j][i], j});
        }

        int k_val = std::min(K_BEST_SERVERS, N);
        std::partial_sort(server_latencies.begin(), server_latencies.begin() + k_val, server_latencies.end());

        best_servers_for_user[i].reserve(k_val);
        for (int k = 0; k < k_val; ++k)
        {
            best_servers_for_user[i].push_back(server_latencies[k].second);
        }
    }
}

// 成本计算函数
CostInfo calculate_cost(const User &user, int server_idx, const Npu &npu)
{
    const auto &server = servers[server_idx];
    int B = std::min(user.remaining_cnt, server.max_b);
    if (B <= 0)
        return {std::numeric_limits<long long>::max(), -1, -1, -1};

    long long arrival_time = user.next_send_time + latencies[server_idx][user.id - 1];
    long long start_time = std::max(arrival_time, npu.free_at);
    long long inference_time = static_cast<long long>(std::ceil(std::sqrt(B) / server.k));
    long long finish_time = start_time + inference_time;
    long long cost = finish_time;

    if (user.last_server_id != -1 && (server.id != user.last_server_id || npu.id_in_server != user.last_npu_id_in_server))
    {
        cost += MIGRATION_PENALTY;
    }
    return {cost, start_time, finish_time, B};
}

// --- 主调度逻辑 ---

int main()
{
    read_input();

    std::vector<std::vector<ScheduledRequest>> solution(M);
    std::priority_queue<UserEvent, std::vector<UserEvent>, std::greater<UserEvent>> user_queue;
    for (int i = 0; i < M; ++i)
    {
        if (users[i].cnt > 0)
        {
            user_queue.push({users[i].s, i});
        }
    }

    while (!user_queue.empty())
    {
        UserEvent current_event = user_queue.top();
        user_queue.pop();

        int user_idx = current_event.user_idx;
        auto &user = users[user_idx];

        // 如果用户在队列中的信息已经过时，则跳过
        if (current_event.time != user.next_send_time)
        {
            continue;
        }

        // 如果用户所有请求已处理完，或下一次发送时间超过了窗口，则跳过
        if (user.remaining_cnt == 0 || user.next_send_time >= user.e)
        {
            continue;
        }

        long long best_cost = std::numeric_limits<long long>::max();
        int best_server_idx = -1;
        int best_B = -1;
        long long best_start_time = -1;
        long long best_finish_time = -1;

        // --- 识别全局最空闲的L个服务器 ---
        std::vector<std::pair<long long, int>> server_availability;
        server_availability.reserve(N);
        for (int s_idx = 0; s_idx < N; ++s_idx)
        {
            if (!server_npus[s_idx].empty())
            {
                server_availability.push_back({server_npus[s_idx].top().free_at, s_idx});
            }
        }
        int l_val = std::min((int)server_availability.size(), L_BEST_AVAILABLE_SERVERS);
        std::partial_sort(server_availability.begin(), server_availability.begin() + l_val, server_availability.end());

        std::vector<int> globally_best_servers;
        globally_best_servers.reserve(l_val);
        for (int k = 0; k < l_val; ++k)
        {
            globally_best_servers.push_back(server_availability[k].second);
        }
        // --- 新增逻辑结束 ---

        // --- 构建更丰富的候选服务器列表 ---
        std::vector<int> candidate_servers = best_servers_for_user[user_idx];
        int last_server_idx = user.last_server_id - 1;
        if (last_server_idx >= 0)
        {
            candidate_servers.push_back(last_server_idx);
        }
        candidate_servers.insert(candidate_servers.end(), globally_best_servers.begin(), globally_best_servers.end());

        // 去重
        std::sort(candidate_servers.begin(), candidate_servers.end());
        candidate_servers.erase(std::unique(candidate_servers.begin(), candidate_servers.end()), candidate_servers.end());
        // --- 修改结束 ---

        for (int server_idx : candidate_servers)
        {
            if (server_npus[server_idx].empty())
                continue;

            const auto &npu = server_npus[server_idx].top();

            CostInfo current = calculate_cost(user, server_idx, npu);

            if (current.cost < best_cost)
            {
                best_cost = current.cost;
                best_server_idx = server_idx;
                best_B = current.B;
                best_start_time = current.start_time;
                best_finish_time = current.finish_time;
            }
        }

        if (best_server_idx != -1)
        {
            auto &server = servers[best_server_idx];

            Npu chosen_npu = server_npus[best_server_idx].top();
            server_npus[best_server_idx].pop();

            solution[user_idx].push_back({user.next_send_time, server.id, chosen_npu.id_in_server, best_B});

            user.remaining_cnt -= best_B;
            user.last_server_id = server.id;
            user.last_npu_id_in_server = chosen_npu.id_in_server;
            user.next_send_time = best_start_time; // 更新为实际开始处理时间

            chosen_npu.free_at = best_finish_time;
            server_npus[best_server_idx].push(chosen_npu);

            // 如果用户还有剩余请求，则再次加入队列
            if (user.remaining_cnt > 0 && user.next_send_time < user.e)
            {
                user_queue.push({user.next_send_time, user_idx});
            }
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