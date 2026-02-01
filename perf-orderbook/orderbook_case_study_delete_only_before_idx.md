# OrderBook 性能指标案例（对标 4-11 风格）

## 测试环境

```
Date: 2026-02-01T09:34:41Z
Linux sharp-game-fades-fin-03 6.8.0-90-generic #91-Ubuntu SMP PREEMPT_DYNAMIC Tue Nov 18 14:14:30 UTC 2025 x86_64 x86_64 x86_64 GNU/Linux
---- lscpu ----
Architecture:                         x86_64
CPU op-mode(s):                       32-bit, 64-bit
Address sizes:                        52 bits physical, 57 bits virtual
Byte Order:                           Little Endian
CPU(s):                               44
On-line CPU(s) list:                  0-43
Vendor ID:                            AuthenticAMD
BIOS Vendor ID:                       QEMU
Model name:                           AMD EPYC 9654 96-Core Processor
BIOS Model name:                      pc-q35-8.2  CPU @ 2.0GHz
BIOS CPU family:                      1
CPU family:                           25
Model:                                17
Thread(s) per core:                   1
Core(s) per socket:                   1
Socket(s):                            44
Stepping:                             1
BogoMIPS:                             4800.00
Flags:                                fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush mmx fxsr sse sse2 syscall nx mmxext fxsr_opt pdpe1gb rdtscp lm rep_good nopl cpuid extd_apicid tsc_known_freq pni pclmulqdq ssse3 fma cx16 pcid sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand hypervisor lahf_lm cmp_legacy svm cr8_legacy abm sse4a misalignsse 3dnowprefetch osvw perfctr_core ssbd ibrs ibpb stibp ibrs_enhanced vmmcall fsgsbase tsc_adjust bmi1 avx2 smep bmi2 erms invpcid avx512f avx512dq rdseed adx smap avx512ifma clflushopt clwb avx512cd sha_ni avx512bw avx512vl xsaveopt xsavec xgetbv1 xsaves avx512_bf16 clzero xsaveerptr wbnoinvd arat npt lbrv nrip_save tsc_scale vmcb_clean flushbyasid pausefilter pfthreshold v_vmsave_vmload vgif vnmi avx512vbmi umip pku ospke avx512_vbmi2 gfni vaes vpclmulqdq avx512_vnni avx512_bitalg avx512_vpopcntdq la57 rdpid fsrm flush_l1d arch_capabilities
Virtualization:                       AMD-V
Hypervisor vendor:                    KVM
Virtualization type:                  full
L1d cache:                            2.8 MiB (44 instances)
L1i cache:                            2.8 MiB (44 instances)
L2 cache:                             22 MiB (44 instances)
L3 cache:                             704 MiB (44 instances)
NUMA node(s):                         1
NUMA node0 CPU(s):                    0-43
Vulnerability Gather data sampling:   Not affected
Vulnerability Itlb multihit:          Not affected
Vulnerability L1tf:                   Not affected
Vulnerability Mds:                    Not affected
Vulnerability Meltdown:               Not affected
Vulnerability Mmio stale data:        Not affected
Vulnerability Reg file data sampling: Not affected
Vulnerability Retbleed:               Not affected
Vulnerability Spec rstack overflow:   Mitigation; Safe RET
Vulnerability Spec store bypass:      Mitigation; Speculative Store Bypass disabled via prctl
Vulnerability Spectre v1:             Mitigation; usercopy/swapgs barriers and __user pointer sanitization
Vulnerability Spectre v2:             Mitigation; Enhanced / Automatic IBRS; IBPB conditional; STIBP disabled; RSB filling; PBRSB-eIBRS Not affected; BHI Not affected
Vulnerability Srbds:                  Not affected
Vulnerability Tsx async abort:        Not affected
Vulnerability Vmscape:                Not affected
---- meminfo ----
MemTotal:       187511724 kB
MemFree:        66825708 kB
MemAvailable:   179363452 kB
```

## 工作负载

- delete-only: 0% add, 100% delete, 0% trade
- 每次迭代操作数: 50000
- 预热订单数: 50000，最大活跃订单数: 50000，价位数: 2000

## 基准输出

```
--------------------------------------------------------------------------------------------------
Benchmark                                        Time             CPU   Iterations UserCounters...
--------------------------------------------------------------------------------------------------
BM_OrderBookDeleteOnly/ops:50000_mean      1568804 ns      1568530 ns            5 items_per_second=31.8782M/s
BM_OrderBookDeleteOnly/ops:50000_median    1561565 ns      1561325 ns            5 items_per_second=32.0241M/s
BM_OrderBookDeleteOnly/ops:50000_stddev      10766 ns        10768 ns            5 items_per_second=218.302k/s
BM_OrderBookDeleteOnly/ops:50000_cv           0.69 %          0.69 %             5 items_per_second=0.68%
```

## 性能指标表

| 指标 | 值 | 说明 |
|---|---:|---|
| Instructions | 81024432383.0 | retired instructions |
| Cycles | 78459711908.0 | CPU cycles |
| IPC | 1.033 | instructions per cycle |
| CPI | 0.968 | cycles per instruction |
| Branch miss rate | 0.797% | branch-misses / branches |
| Cache miss rate | 9.534% | cache-misses / cache-references |
| L1D MPKI | 93.97 | L1-dcache-load-misses per 1K instr |
| L1I MPKI | 0.043 | L1-icache-load-misses per 1K instr |
| LLC MPKI | N/A | LLC-load-misses per 1K instr |
| dTLB MPKI | 0.034 | dTLB-load-misses per 1K instr |
| iTLB MPKI | 0.001 | iTLB-load-misses per 1K instr |

## 结论与分析

- IPC 约 1.03，Cache miss rate 9.53% 与 L1D MPKI 93.97 依然偏高，delete-only 仍是最明显的内存瓶颈路径。  
- 吞吐提升到 31.88M/s（相对优化前 28.83M/s 有明显提升），说明删除路径的结构性改动有效降低了部分控制与探测开销。  
- 分支误预测率降到 0.80%，表明删除路径的分支行为更稳定。  
- 典型热点仍在 `deleteOrder` → `findOrderIndex` → `order_map_` 清理/探测 → 价位链表调整。  
- 后续优化方向依旧是减少哈希探测与缓存未命中（例如更紧凑的哈希表布局或索引化存储）。  
