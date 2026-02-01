# OrderBook 性能指标案例

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

- add-only: 100% add, 0% delete, 0% trade
- 每次迭代操作数: 50000
- 预热订单数: 0，最大活跃订单数: 50000，价位数: 2000

## 基准输出

```
-----------------------------------------------------------------------------------------------
Benchmark                                     Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------
BM_OrderBookAddOnly/ops:50000_mean      2646992 ns      2647134 ns            5 items_per_second=18.8884M/s
BM_OrderBookAddOnly/ops:50000_median    2646922 ns      2647072 ns            5 items_per_second=18.8888M/s
BM_OrderBookAddOnly/ops:50000_stddev       5404 ns         5377 ns            5 items_per_second=38.3608k/s
BM_OrderBookAddOnly/ops:50000_cv           0.20 %          0.20 %             5 items_per_second=0.20%
```

## 性能指标表

| 指标 | 值 | 说明 |
|---|---:|---|
| Instructions | 30083073914.0 | retired instructions |
| Cycles | 20544461587.0 | CPU cycles |
| IPC | 1.464 | instructions per cycle |
| CPI | 0.683 | cycles per instruction |
| Branch miss rate | 1.107% | branch-misses / branches |
| Cache miss rate | 1.202% | cache-misses / cache-references |
| L1D MPKI | 66.90 | L1-dcache-load-misses per 1K instr |
| L1I MPKI | 0.031 | L1-icache-load-misses per 1K instr |
| LLC MPKI | N/A | LLC-load-misses per 1K instr |
| dTLB MPKI | 0.043 | dTLB-load-misses per 1K instr |
| iTLB MPKI | 0.001 | iTLB-load-misses per 1K instr |

## 结论与分析

- IPC 1.464 是三类负载中最高，说明 add 路径更多受计算/控制流驱动，内存阻塞相对少。  
- Cache miss rate 1.20% 最低，表明 add-only 的整体局部性最好，可视为结构优化的“上限参考”。  
- L1D MPKI 仍有 66.9，说明哈希探测与价位链表插入仍带来明显的指针追逐成本。  
- 典型热点路径：`addOrder` → `findOrderIndex`/`addPriceLevel`/内存池分配。  
- 优化指向：减少哈希探测次数、提高价位结构局部性（例如数组化价位或压缩索引）。  
