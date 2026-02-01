# OrderBook 性能指标

## 测试环境

```
Date: 2026-02-01T09:25:48Z
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
MemFree:        68180964 kB
MemAvailable:   180717408 kB
```

## 工作负载

- add/delete 各占一半，trade 占比 10%
- 每次迭代操作数: 50000
- 预热订单数: 20000，最大活跃订单数: 50000，价位数: 2000

## 基准输出

```
------------------------------------------------------------------------------------------------------
Benchmark                                            Time             CPU   Iterations UserCounters...
------------------------------------------------------------------------------------------------------
BM_OrderBookWorkload/ops:50000/trade_pct:10    1759782 ns      1759572 ns          804 items_per_second=28.416M/s
```

## 性能指标表

| 指标 | 值 | 说明 |
|---|---:|---|
| Instructions | 15144237040.0 | retired instructions |
| Cycles | 14817131072.0 | CPU cycles |
| IPC | 1.022 | instructions per cycle |
| CPI | 0.978 | cycles per instruction |
| Branch miss rate | 1.507% | branch-misses / branches |
| Cache miss rate | 6.941% | cache-misses / cache-references |
| L1D MPKI | 78.02 | L1-dcache-load-misses per 1K instr |
| L1I MPKI | 0.049 | L1-icache-load-misses per 1K instr |
| LLC MPKI | N/A | LLC-load-misses per 1K instr |
| dTLB MPKI | 0.022 | dTLB-load-misses per 1K instr |
| iTLB MPKI | 0.002 | iTLB-load-misses per 1K instr |

## 结论

- IPC ≈ 1.02，整体接近“每周期一条指令”，说明该负载的瓶颈更可能来自内存访问或分支控制，而非纯算力不足。  
- 分支误预测率 1.51% 属于中等水平，对应大量 add/delete 的控制流并不完全可预测，但不是首要瓶颈。  
- L1D MPKI = 78.02 明显偏高，数据局部性不足是主要信号，符合订单簿哈希/链表遍历的访问模式特征。  
- L1I MPKI 与 TLB MPKI 极低，前端与地址转换不是当前瓶颈。  
- LLC 事件在该平台 perf 不支持（N/A），需要用替代事件或采样工具验证是否存在更深层缓存压力。  
