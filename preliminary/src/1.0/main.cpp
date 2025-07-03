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
            npus.push_back({servers[i].id, j + 1, 0});
        }
    }
}

// --- 主调度逻辑 ---

int main() // 主函数。贪心
{
    read_input();

    std::vector<std::vector<ScheduledRequest>> solution(M);
    long long total_remaining_cnt = 0;
    for (const auto &user : users)
    {
        total_remaining_cnt += user.cnt; // 主循环终止条件
    }

    // 贪心调度
    while (total_remaining_cnt > 0)                                  // 一直执行到所有用户的全部样本被调度完毕
    {                                                                // 每一轮循环：在当前时间点从所有可能的(用户, NPU, BatchSize)组合中，找到一个最优的组合，并进行调度
        long long best_cost = std::numeric_limits<long long>::max(); // 最优组合的代价
        int best_user_idx = -1;                                      // 最优组合的用户id
        int best_npu_idx = -1;                                       // 最优组合的NPU id
        int best_B = -1;                                             // 最优组合的BatchSize
        long long best_finish_time = -1;                             // 最优组合的完成时间

        // 找到下一个要调度的请求
        for (int i = 0; i < M; ++i)
        {
            if (users[i].remaining_cnt == 0)
                continue; // i的已处理完，直接跳过

            long long send_time = users[i].next_send_time;

            for (size_t j = 0; j < npus.size(); ++j)
            {
                int server_idx = npus[j].server_id - 1;

                int B = std::min(users[i].remaining_cnt, servers[server_idx].max_b);
                if (B <= 0)
                    continue;

                long long arrival_time = send_time + latencies[server_idx][i];
                long long start_time = std::max(arrival_time, npus[j].free_at);                                     // 任务实际开始时间。取决于任务到达时间与NPU空闲时间
                long long inference_time = static_cast<long long>(std::ceil(std::sqrt(B) / servers[server_idx].k)); // 推理时间
                long long finish_time = start_time + inference_time;                                                // 任务完成时间

                long long cost = finish_time;

                // 迁移惩罚。如果任务迁移到其他NPU，则需要额外增加x时间单位的惩罚
                const int MIGRATION_PENALTY = 20; // 启发式值。默认设定为50，可以根据具体数据进行调整
                if (users[i].last_server_id != -1 && (npus[j].server_id != users[i].last_server_id || npus[j].id_in_server != users[i].last_npu_id_in_server))
                {
                    cost += MIGRATION_PENALTY;
                }

                if (cost < best_cost)
                {
                    best_cost = cost;
                    best_user_idx = i;
                    best_npu_idx = j;
                    best_B = B; // i, j, B是当前最优选择
                    best_finish_time = finish_time;
                }
            }
        }

        if (best_user_idx != -1) // 找到一个可行的调度，正式执行它
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
        }
        else
        {
            // 如果所有用户的样本都已处理完，则跳出循环
            break;
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
                      << solution[i][j].B << (j == solution[i].size() - 1 ? "" : " ");
        }
        std::cout << "\n";
    }

    return 0;
}