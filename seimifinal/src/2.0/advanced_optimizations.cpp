// 高级优化示例代码片段

// 动态权重调整策略
struct DynamicWeights {
    long long deadline_penalty;
    int migration_penalty;
    int load_balance_weight;

    void update(long long current_time, const std::vector<User>& users) {
        // 根据系统负载动态调整权重
        int urgent_users = 0;
        for (const auto& user : users) {
            if (user.remaining_cnt > 0 && user.urgency > 1.0) {
                urgent_users++;
            }
        }

        // 紧急用户多时，增加截止时间惩罚权重
        if (urgent_users > users.size() * 0.3) {
            deadline_penalty = 2000;  // 增加惩罚
            migration_penalty = 10;   // 减少迁移惩罚，允许更多迁移
        } else {
            deadline_penalty = 1000;  // 正常惩罚
            migration_penalty = 20;   // 正常迁移惩罚
        }
    }
};

// 预测性资源预留
class ResourcePredictor {
public:
    // 预测即将紧急的用户
    std::vector<int> predict_urgent_users(long long current_time,
                                         const std::vector<User>& users) {
        std::vector<int> urgent_candidates;

        for (int i = 0; i < users.size(); ++i) {
            if (users[i].remaining_cnt <= 0) continue;

            long long time_to_deadline = users[i].e - current_time;
            long long estimated_completion_time = estimate_completion_time(i, users[i]);

            // 如果预计完成时间接近截止时间，标记为紧急
            if (estimated_completion_time > time_to_deadline * 0.8) {
                urgent_candidates.push_back(i);
            }
        }

        return urgent_candidates;
    }

private:
    long long estimate_completion_time(int user_idx, const User& user) {
        // 简化的完成时间估计
        // 实际应考虑服务器性能、网络延迟等因素
        return user.remaining_cnt * 50; // 粗略估计
    }
};

// 智能batch分割策略
class BatchSplitStrategy {
public:
    struct SplitPlan {
        std::vector<int> batch_sizes;
        std::vector<long long> send_times;
    };

    SplitPlan optimize_batch_split(int total_samples,
                                  long long available_time,
                                  int max_requests,
                                  double server_efficiency) {
        SplitPlan plan;

        if (max_requests <= 0 || total_samples <= 0) return plan;

        // 策略1: 如果时间充裕，使用多个较小batch获得更好效率
        if (available_time > total_samples * 100) {
            int optimal_batch = find_efficiency_optimal_batch(server_efficiency);
            while (total_samples > 0 && plan.batch_sizes.size() < max_requests) {
                int batch = std::min(total_samples, optimal_batch);
                plan.batch_sizes.push_back(batch);
                total_samples -= batch;
            }
        }
        // 策略2: 时间紧张，使用较大batch快速完成
        else {
            int avg_batch = std::ceil((double)total_samples / max_requests);
            while (total_samples > 0 && plan.batch_sizes.size() < max_requests) {
                int batch = std::min(total_samples, avg_batch);
                plan.batch_sizes.push_back(batch);
                total_samples -= batch;
            }
        }

        return plan;
    }

private:
    int find_efficiency_optimal_batch(double server_efficiency) {
        // 根据服务器效率曲线找到最优batch size
        // 这里简化为固定值，实际应该根据效率函数计算
        return 100;
    }
};

// 负载均衡优化器
class LoadBalancer {
public:
    int select_best_npu(const std::vector<Npu>& npus,
                       int preferred_server = -1) {
        if (npus.empty()) return -1;

        // 如果有偏好服务器且负载不太重，优先选择
        if (preferred_server > 0) {
            int best_npu_in_preferred = -1;
            long long min_utilization = LLONG_MAX;

            for (int i = 0; i < npus.size(); ++i) {
                if (npus[i].server_id == preferred_server &&
                    npus[i].utilization_time < min_utilization) {
                    min_utilization = npus[i].utilization_time;
                    best_npu_in_preferred = i;
                }
            }

            // 如果偏好服务器的NPU负载不太重，就选择它
            if (best_npu_in_preferred >= 0 &&
                min_utilization < get_average_utilization(npus) * 1.5) {
                return best_npu_in_preferred;
            }
        }

        // 否则选择全局最空闲的NPU
        int best_npu = 0;
        for (int i = 1; i < npus.size(); ++i) {
            if (npus[i].free_at < npus[best_npu].free_at ||
                (npus[i].free_at == npus[best_npu].free_at &&
                 npus[i].utilization_time < npus[best_npu].utilization_time)) {
                best_npu = i;
            }
        }

        return best_npu;
    }

private:
    double get_average_utilization(const std::vector<Npu>& npus) {
        if (npus.empty()) return 0;

        long long total = 0;
        for (const auto& npu : npus) {
            total += npu.utilization_time;
        }
        return (double)total / npus.size();
    }
};
