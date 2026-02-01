# Delete-only 优化记录（2026-02-01）

## 目标
- 优化 delete-only 路径的吞吐与缓存行为。
- 基准：`BM_OrderBookDeleteOnly/ops:50000`，`--benchmark_repetitions=5`。

---

## 方案一：墓碑删除（tombstone）
### 方法
- 用 `kOrderIndexDeleted` 标记删除槽位，避免 `backwardShiftDelete` 的连续移动。
- 查找时跳过 tombstone；插入时优先复用 tombstone。

### 结果
- 吞吐：**28.83M/s → 31.88M/s**（+10.6%）
- IPC：**1.023 → 1.033**
- Branch miss rate：**1.061% → 0.797%**
- Cache miss rate：**10.30% → 9.53%**
- L1D MPKI：**93.98 → 93.97**（基本不变）

### 结论
- tombstone 明显提高吞吐，分支与整体 miss 率下降。
- L1D MPKI 仍高，说明哈希探测/链表仍是主要瓶颈。

---

## 方案二：index 化（map 存索引）
### 方法
- `order_map_` 存储 `uint32_t idx`，通过 `order_pool_->getByIndex(idx)` 访问对象。
- `order_keys_` 做 key 判断。

### 结果
- 吞吐：**31.88M/s → 30.13M/s**（回退）
- IPC：**1.033 → 1.084**
- Branch miss rate：**0.797% → 0.744%**
- Cache miss rate：**9.53% → 10.10%**
- L1D MPKI：**93.97 → 90.49**（下降）

### 结论
- 局部性改善，但吞吐回退，说明索引解码/访问成本抵消收益。

---

## 方案三：紧凑 entry（key+idx 合并）
### 方法
- 将 `order_map_` 改为 `OrderMapEntry{key, idx}`，减少探测时跨数组访问。

### 结果
- 吞吐：**30.13M/s → 29.33M/s**（继续回退）
- IPC：**1.084 → 1.090**
- Branch miss rate：**0.744% → 0.765%**
- Cache miss rate：**10.10% → 9.50%**
- L1D MPKI：**90.49 → 87.78**（下降）

### 结论
- 缓存局部性继续改善，但吞吐下降更明显。
- 索引化更适合“降 miss”，不适合“最高吞吐”。

---

## 总结结论
- **最优吞吐方案：墓碑删除（tombstone）**。
- index 化路线改善了缓存指标，但吞吐持续下降。
- 如果目标是吞吐，建议回退到 tombstone 版本；如果目标是降低 miss，可继续深挖索引化但需进一步减轻访问开销。

## 相关报告
- `perf-orderbook/orderbook_case_study_delete_only_before.md`（墓碑前）
- `perf-orderbook/orderbook_case_study_delete_only_before_idx.md`（索引化前）
- `perf-orderbook/orderbook_case_study_delete_only_before_packed.md`（紧凑 entry 前）
- `perf-orderbook/orderbook_case_study_delete_only.md`（当前）
