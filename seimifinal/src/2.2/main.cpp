#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <queue>
#include <map>
#include <set>
#include <climits>

using namespace std;

struct Server {
    int npus;
    int speed_coef;
    int memory;
};

struct User {
    int start_time;
    int end_time;
    int sample_count;
    int memory_a;
    int memory_b;
    double priority; // 用户优先级
};

struct Task {
    int time;
    int server;
    int npu;
    int batch;
};

struct NPULoad {
    int total_load = 0;
    int last_available_time = 0;
};

vector<Server> servers;
vector<User> users;
vector<vector<int>> latency;
vector<vector<NPULoad>> npu_loads;

int calculate_inference_time(int batch_size, int speed_coef) {
    return (int)ceil((double)batch_size / (speed_coef * sqrt(batch_size)));
}

// 计算最大可用批次大小
int get_max_batch_size(int user_id, int server_id) {
    const User& user = users[user_id];
    const Server& server = servers[server_id];
    
    if (user.memory_a <= 0) return 0;
    
    int max_by_memory = (server.memory - user.memory_b) / user.memory_a;
    return max(0, min({max_by_memory, 1000}));
}

// 计算服务器-NPU组合的适合度
double calculate_fitness(int user_id, int server_id, int npu_id) {
    const User& user = users[user_id];
    const Server& server = servers[server_id];
    
    double speed_factor = server.speed_coef;
    double latency_factor = 1000.0 / (latency[server_id][user_id] + 1);
    double memory_factor = get_max_batch_size(user_id, server_id) / 1000.0;
    double load_factor = 1000.0 / (npu_loads[server_id][npu_id].total_load + 1);
    double priority_factor = user.priority;
    
    return speed_factor * latency_factor * memory_factor * load_factor * priority_factor;
}

// 智能批次大小选择
int select_optimal_batch_size(int user_id, int server_id, int remaining_samples) {
    int max_batch = get_max_batch_size(user_id, server_id);
    if (max_batch <= 0) return 0;
    
    // 考虑多个因素选择批次大小
    int conservative_batch = min(max_batch, remaining_samples);
    
    // 如果剩余样本很少，直接处理
    if (remaining_samples <= max_batch) {
        return remaining_samples;
    }
    
    // 使用动态批次大小策略
    int optimal_batch;
    if (remaining_samples > max_batch * 3) {
        // 大量样本：使用较大批次
        optimal_batch = max_batch;
    } else if (remaining_samples > max_batch) {
        // 中等样本：平衡批次大小
        optimal_batch = min(max_batch, remaining_samples / 2);
    } else {
        // 少量样本：一次处理
        optimal_batch = remaining_samples;
    }
    
    return max(1, min(optimal_batch, max_batch));
}

// 为用户生成优化的调度方案
vector<Task> generate_optimized_schedule(int user_id) {
    vector<Task> schedule;
    const User& user = users[user_id];
    
    int remaining_samples = user.sample_count;
    int current_time = user.start_time;
    int last_server = -1, last_npu = -1;
    
    while (remaining_samples > 0 && current_time < user.end_time) {
        vector<tuple<double, int, int>> candidates;
        
        // 评估所有服务器-NPU组合
        for (int s = 0; s < servers.size(); s++) {
            for (int n = 0; n < servers[s].npus; n++) {
                double fitness = calculate_fitness(user_id, s, n);
                
                // 给继续使用同一NPU的选择额外加分（减少迁移）
                if (s == last_server && n == last_npu) {
                    fitness *= 1.2;
                }
                
                candidates.push_back({fitness, s, n});
            }
        }
        
        // 按适合度排序
        sort(candidates.rbegin(), candidates.rend());
        
        bool scheduled = false;
        
        // 尝试前几个最优选择
        for (int i = 0; i < min(5, (int)candidates.size()) && !scheduled; i++) {
            int server_id = get<1>(candidates[i]);
            int npu_id = get<2>(candidates[i]);
            
            int batch_size = select_optimal_batch_size(user_id, server_id, remaining_samples);
            
            if (batch_size > 0) {
                int arrival_time = current_time + latency[server_id][user_id];
                int inference_time = calculate_inference_time(batch_size, servers[server_id].speed_coef);
                
                // 检查时间约束
                if (arrival_time + inference_time <= user.end_time + 5000) { // 给一些缓冲
                    Task task;
                    task.time = current_time;
                    task.server = server_id + 1;
                    task.npu = npu_id + 1;
                    task.batch = batch_size;
                    
                    schedule.push_back(task);
                    
                    // 更新状态
                    npu_loads[server_id][npu_id].total_load += batch_size;
                    npu_loads[server_id][npu_id].last_available_time = arrival_time + inference_time;
                    
                    remaining_samples -= batch_size;
                    current_time += latency[server_id][user_id] + 1;
                    last_server = server_id;
                    last_npu = npu_id;
                    scheduled = true;
                }
            }
        }
        
        // 如果无法调度，尝试等待一段时间
        if (!scheduled) {
            current_time += 100;
            if (current_time >= user.end_time) break;
        }
    }
    
    return schedule;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    
    // 读取输入
    int N;
    cin >> N;
    
    servers.resize(N);
    npu_loads.resize(N);
    
    for (int i = 0; i < N; i++) {
        cin >> servers[i].npus >> servers[i].speed_coef >> servers[i].memory;
        npu_loads[i].resize(servers[i].npus);
    }
    
    int M;
    cin >> M;
    
    users.resize(M);
    for (int i = 0; i < M; i++) {
        cin >> users[i].start_time >> users[i].end_time >> users[i].sample_count;
        // 计算用户优先级（紧急程度）
        users[i].priority = (double)users[i].sample_count / (users[i].end_time - users[i].start_time + 1);
    }
    
    latency.resize(N, vector<int>(M));
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < M; j++) {
            cin >> latency[i][j];
        }
    }
    
    for (int i = 0; i < M; i++) {
        cin >> users[i].memory_a >> users[i].memory_b;
    }
    
    // 按优先级排序用户
    vector<pair<double, int>> user_order;
    for (int i = 0; i < M; i++) {
        user_order.push_back({users[i].priority, i});
    }
    sort(user_order.rbegin(), user_order.rend());
    
    // 生成调度方案
    vector<vector<Task>> all_schedules(M);
    for (const auto& user_pair : user_order) {
        int user_id = user_pair.second;
        all_schedules[user_id] = generate_optimized_schedule(user_id);
    }
    
    // 输出结果
    for (int i = 0; i < M; i++) {
        const vector<Task>& schedule = all_schedules[i];
        
        cout << schedule.size() << "\n";
        
        for (int j = 0; j < schedule.size(); j++) {
            const Task& task = schedule[j];
            cout << task.time << " " << task.server << " " << task.npu << " " << task.batch;
            if (j < schedule.size() - 1) {
                cout << " ";
            }
        }
        cout << "\n";
    }
    
    return 0;
}
