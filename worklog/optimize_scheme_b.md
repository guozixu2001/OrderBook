# Scheme B (Chunked Arena + Dynamic Hash) — Worklog

## 优化方案
- 目标：降低指针追逐和页面碎片，提升访问局部性与可预取性。
- 方案：
  - **OrderArena**：分段数组（chunked arena），每个 chunk 64K Orders，分配返回 index。
  - **OrderIndexMap**：动态开地址哈希（power‑of‑two），存 `order_id -> index`，支持 tombstone 和 rehash。
  - **Order 不搬迁**：rehash 只搬哈希表，Order 索引稳定。

## 实现摘要
- 新增 `OrderArena` 与 `OrderIndexMap`，替换 `TieredMemoryPool<Order>` 与 `OrderHashMap`。
- OrderBook 在 add/modify/delete/trade 时改为 `map.find -> arena.get` 流程。
- clear 重置 arena + map，不再逐一回收 Order。

## 性能结果（workload: 50k ops, trade 10%, repetitions=5）
- **吞吐**：25.0797M/s（新）vs 28.416M/s（基线） → **-11.7%**
- **IPC**：1.137 vs 1.022 → **+11.3%**
- **Cache miss rate**：5.412% vs 6.941% → **-22.0%**
- **L1D MPKI**：77.79 vs 78.02 → **-0.3%**（基本持平）
- **dTLB MPKI**：0.009 vs 0.022 → **-59%**

## 结论
- 数据局部性指标明显改善（IPC↑、cache miss↓、dTLB miss↓），符合“连续结构 + 指针减少”的预期。
- 但总体吞吐下降，说明新哈希路径引入了额外成本（rehash/探测/校验），抵消了局部性收益。
- L1D MPKI 几乎未变，说明 L1D 层面的随机访问仍是主要瓶颈。

## 下一步优化方向（优先级高 → 低）
1. **避免热路径 rehash**：初始化更大的 `OrderIndexMap` 容量（如 2x max_active），或单独提供 reserve。
2. **增量 rehash**：将一次性 rehash 改为分步迁移，减少峰值开销。
3. **降低探测成本**：降低 load factor（<=0.65），或引入 8-bit fingerprint。
4. **减少二次探测**：合并 find+insert 路径，避免重复 probe。

## 迭代 1：预扩容 order_map_（2x MAX_ORDERS）
- 变更：`ORDER_MAP_INITIAL_CAPACITY = MAX_ORDERS * 2`，降低 load factor，期望减少 probe/rehash。
- 结果：吞吐 **24.9627M/s**（vs 25.0797M/s，约 -0.47%）；相对基线 28.416M/s 仍 **-12.2%**。
- 指标变化：
  - IPC 1.136（略升）
  - Cache miss rate 5.601%（仍低于基线 6.941%）
  - **dTLB MPKI 升至 0.652**（显著增加，可能由更大表造成页工作集扩大）
- 结论：预扩容没有带来吞吐改善，反而扩大了页工作集。暂不推荐保留。

## 迭代 2：去掉 addOrder 的二次探测（find + insert）
- 变更：`OrderIndexMap::insert` 返回插入结果，`addOrder` 直接 insert，避免重复 probe；遇到已存在或失败时回滚 arena。
- 结果：吞吐 **25.6573M/s**（vs 24.9627M/s，**+2.78%**）；相对基线 28.416M/s 仍 **-9.7%**。
- 指标变化：
  - IPC 1.099（略升）
  - Cache miss rate 5.422%（仍低于基线 6.941%）
  - **L1D MPKI 80.93**（略升）
  - **dTLB MPKI 0.749**（仍偏高）
- 结论：减少重复探测带来小幅吞吐改善，但仍明显落后基线；TLB/工作集压力仍是主要问题。

## 迭代 3：回退 order_map_ 预扩容
- 变更：`ORDER_MAP_INITIAL_CAPACITY` 从 `MAX_ORDERS * 2` 回退到 `MAX_ORDERS`，减少页工作集。
- 状态：待跑基准验证吞吐与 dTLB MPKI 是否回落。

- 结果：吞吐 **25.6573M/s**（与迭代 2 基本一致）；相对基线 28.416M/s 仍 **-9.7%**。
- 指标：L1D MPKI 80.93、dTLB MPKI 0.749（未明显回落）。
- 结论：回退预扩容未改善 TLB/L1D 指标，吞吐几乎不变。

## 实验：max_active = 40k
- 变更：bench workload `max_active` 从 50k 改为 40k（mixed/add/delete/trade）。
- 结果：吞吐 **25.6726M/s**（与 50k 基本持平，+0.06%）。
- 指标：dTLB MPKI **0.026**（显著回落接近基线 0.022），L1D MPKI **81.42**（仍偏高）。
- 结论：降低活跃订单能明显缓解 TLB 压力，但整体吞吐提升有限，主要瓶颈仍在 L1D 随机访问与哈希探测成本。

## 方案 1：SwissTable 风格 ctrl + fingerprint
- 变更：order_map_ 改为 `ctrl[] + keys[] + idxs[]`（SoA），ctrl 使用 7-bit fingerprint + EMPTY/TOMB 标记，按 16-slot group 扫描。
- 结果（max_active=40k）：吞吐 **24.0777M/s**（较 25.6726M/s **-6.2%**，相对基线 **-15.3%**）。
- 指标：IPC 1.121（略升），dTLB MPKI 0.009（改善），**L1D MPKI 81.28（仍偏高）**。
- 结论：控制字节减少了部分高层 miss，但 group 扫描与更多分支判断引入额外开销，吞吐下降。
