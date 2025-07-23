# 华为嵌入式软件大赛复赛 - 2.0版本解决方案

## 解决方案概述

2.0版本是一个基于动态调度的解决方案，采用了多因素成本函数和概率采样策略，旨在优化服务器资源利用，减少迁移次数，并确保用户请求在截止时间内完成。

## 核心思路

1. **多因素成本函数**：综合考虑多种因素进行决策，包括：
   - 截止时间惩罚（非线性惩罚超时）
   - 用户紧急度
   - 批处理效率
   - 迁移惩罚
   - 负载均衡
   - 服务器匹配度

2. **基于概率分布的调度**：不单纯选择成本最低的方案，而是根据成本构建概率分布，使用softmax函数进行随机采样，避免局部最优。

3. **动态优先级**：根据用户紧急度动态调整优先级，紧急用户优先调度。

4. **智能批处理（Batch）选择**：根据剩余样本和时间窗口动态调整批处理大小。

## 主要特性

### 1. 高效的资源分配

- 预计算每个服务器不同批处理大小的效率
- 智能选择最优批处理大小，平衡吞吐量和响应时间
- 考虑用户特定的显存需求约束

### 2. 动态调度策略

- 实时更新用户紧急度：`urgency = remaining_cnt / remaining_time`
- 基于紧急度排序用户，确保及时处理高风险用户
- 使用概率分布进行NPU选择，避免过度集中

### 3. 优化的成本函数

```cpp
// 成本函数组成部分
long long cost = finish_time;

// 1. 截止时间惩罚 (非线性)
if (time_over_deadline > 0) {
    cost += time_over_deadline * time_over_deadline / 1000 + time_over_deadline * DEADLINE_PENALTY_WEIGHT;
}

// 2. 紧急度因子
if (remaining_time < 10000) { // 时间紧张时
    cost = static_cast<long long>(cost * (1.0 + users[i].urgency * 0.1));
}

// 3. 效率奖励 - 选择高效batch的奖励
double efficiency_bonus = servers[server_idx].efficiency[optimal_B] * 10;
cost -= static_cast<long long>(efficiency_bonus);

// 4. 迁移惩罚 (渐进式)
if (users[i].last_server_id != -1 && 
    (npus[j].server_id != users[i].last_server_id || 
     npus[j].id_in_server != users[i].last_npu_id_in_server)) {
    // 根据已发送请求数量调整迁移惩罚
    int sent_requests = solution[i].size();
    int migration_penalty = MIGRATION_PENALTY * (1 + sent_requests / 10);
    cost += migration_penalty;
}

// 5. 负载均衡 (考虑相对负载)
double relative_load = npus[j].utilization_time - avg_utilization;
cost += static_cast<long long>(relative_load * LOAD_BALANCE_WEIGHT);

// 6. 服务器匹配度奖励
if (npus[j].server_id == users[i].last_server_id) {
    cost /= 50; // 继续使用同一服务器的奖励
}
```

### 4. 概率采样方法

采用了自适应的概率分布策略，针对不同场景使用不同的概率计算方法：

- **单一选择**：直接选择唯一有效选项
- **少量选项**（≤50）：基于排名的概率分布，给探索更多机会
- **大量选项**（>50）：基于成本归一化的softmax概率分布

### 5. 高级优化（advanced_optimizations.cpp）

- **动态权重调整**：根据系统负载动态调整各因素权重
- **预测性资源预留**：预测即将紧急的用户并提前为其预留资源
- **智能批处理分割**：根据时间窗口和服务器效率优化批处理分割策略
- **负载均衡优化**：在保持用户亲和性的同时实现全局负载均衡

## 解决方案特点

- **鲁棒性**：包含死循环处理机制，即使遇到极端情况也能正常工作
- **可调节性**：通过调整权重常量可以调整不同因素的重要性
- **探索性**：概率采样方法允许一定程度的随机探索，避免陷入局部最优
- **适应性**：根据当前系统状态动态调整策略

## 改进方向

1. 进一步优化批处理大小的选择策略
2. 实现更复杂的用户请求预测模型
3. 探索更高级的负载均衡算法
4. 优化概率分布计算方法，提高采样效率
5. 增加自适应学习机制，根据历史表现调整参数 