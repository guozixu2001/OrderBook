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
BM_OrderBookDeleteOnly/ops:50000_mean      1659821 ns      1659486 ns            5 items_per_second=30.1302M/s
BM_OrderBookDeleteOnly/ops:50000_median    1662212 ns      1661873 ns            5 items_per_second=30.0865M/s
BM_OrderBookDeleteOnly/ops:50000_stddev       6285 ns         6295 ns            5 items_per_second=114.591k/s
BM_OrderBookDeleteOnly/ops:50000_cv           0.38 %          0.38 %             5 items_per_second=0.38%
```

## 性能指标表

| 指标 | 值 | 说明 |
|---|---:|---|
| Instructions | 82664768629.0 | retired instructions |
| Cycles | 76228260595.0 | CPU cycles |
| IPC | 1.084 | instructions per cycle |
| CPI | 0.922 | cycles per instruction |
| Branch miss rate | 0.744% | branch-misses / branches |
| Cache miss rate | 10.10% | cache-misses / cache-references |
| L1D MPKI | 90.49 | L1-dcache-load-misses per 1K instr |
| L1I MPKI | 0.040 | L1-icache-load-misses per 1K instr |
| LLC MPKI | N/A | LLC-load-misses per 1K instr |
| dTLB MPKI | 0.022 | dTLB-load-misses per 1K instr |
| iTLB MPKI | 0.001 | iTLB-load-misses per 1K instr |

## 结论与分析

- 本轮“index 化”后吞吐下降到 30.13M/s（相对上轮 31.88M/s 回退），说明额外的索引解码/访问成本抵消了收益。  
- IPC 上升到 1.084、分支误预测降到 0.744%，同时 L1D MPKI 下降到 90.49，显示局部性略有改善。  
- Cache miss rate 升到 10.10%，整体仍是内存瓶颈路径。  
- 结论：index 化带来了“局部性改善但吞吐回退”的结果，需要进一步优化索引访问路径或回退到前一版。  
