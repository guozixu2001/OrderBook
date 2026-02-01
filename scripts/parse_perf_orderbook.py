#!/usr/bin/env python3
import argparse
import math
import re
from pathlib import Path

def parse_perf_csv(path: Path):
    events = {}
    for line in path.read_text().splitlines():
        if not line.strip():
            continue
        # Skip perf header or other non-metric lines.
        if not re.match(r"^[0-9<]", line.strip()):
            continue
        parts = [p.strip() for p in line.split(',')]
        if len(parts) < 3:
            continue
        value, unit, event = parts[0], parts[1], parts[2]
        if value in ("<not supported>", "<not counted>"):
            events[event] = None
            continue
        try:
            num = float(value.replace(',', ''))
        except ValueError:
            events[event] = None
            continue
        events[event] = (num, unit)
    return events


def get_value(events, key):
    data = events.get(key)
    if not data:
        return None
    return data[0]


def ratio(numerator, denominator):
    if numerator is None or denominator in (None, 0):
        return None
    return numerator / denominator


def mpki(misses, instructions):
    if misses is None or instructions in (None, 0):
        return None
    return 1000.0 * misses / instructions


def fmt(val, unit="", digits=3):
    if val is None or (isinstance(val, float) and (math.isnan(val) or math.isinf(val))):
        return "N/A"
    if abs(val) >= 100:
        return f"{val:.1f}{unit}"
    if abs(val) >= 10:
        return f"{val:.2f}{unit}"
    return f"{val:.{digits}f}{unit}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--perf", required=True)
    ap.add_argument("--system", required=True)
    ap.add_argument("--bench", required=True)
    ap.add_argument("--ops", type=int, required=True)
    ap.add_argument("--trade", type=int, required=True)
    ap.add_argument("--workload", default="")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    events = parse_perf_csv(Path(args.perf))

    cycles = get_value(events, "cycles")
    instructions = get_value(events, "instructions")
    branches = get_value(events, "branches")
    branch_misses = get_value(events, "branch-misses")
    cache_refs = get_value(events, "cache-references")
    cache_misses = get_value(events, "cache-misses")

    l1d_miss = get_value(events, "L1-dcache-load-misses")
    l1i_miss = get_value(events, "L1-icache-load-misses")
    llc_miss = get_value(events, "LLC-load-misses")
    dtlb_miss = get_value(events, "dTLB-load-misses")
    itlb_miss = get_value(events, "iTLB-load-misses")

    ipc = ratio(instructions, cycles)
    cpi = ratio(cycles, instructions)
    br_miss_rate = ratio(branch_misses, branches)
    cache_miss_rate = ratio(cache_misses, cache_refs)

    metrics = [
        ("Instructions", fmt(instructions), "retired instructions"),
        ("Cycles", fmt(cycles), "CPU cycles"),
        ("IPC", fmt(ipc), "instructions per cycle"),
        ("CPI", fmt(cpi), "cycles per instruction"),
        ("Branch miss rate", fmt(br_miss_rate * 100 if br_miss_rate is not None else None, "%"), "branch-misses / branches"),
        ("Cache miss rate", fmt(cache_miss_rate * 100 if cache_miss_rate is not None else None, "%"), "cache-misses / cache-references"),
        ("L1D MPKI", fmt(mpki(l1d_miss, instructions)), "L1-dcache-load-misses per 1K instr"),
        ("L1I MPKI", fmt(mpki(l1i_miss, instructions)), "L1-icache-load-misses per 1K instr"),
        ("LLC MPKI", fmt(mpki(llc_miss, instructions)), "LLC-load-misses per 1K instr"),
        ("dTLB MPKI", fmt(mpki(dtlb_miss, instructions)), "dTLB-load-misses per 1K instr"),
        ("iTLB MPKI", fmt(mpki(itlb_miss, instructions)), "iTLB-load-misses per 1K instr"),
    ]

    # Simple conclusions based on heuristics
    conclusions = []
    if ipc is not None and ipc < 1.0:
        conclusions.append("IPC 偏低，可能受到分支、缓存或数据依赖的限制。")
    if br_miss_rate is not None and br_miss_rate > 0.03:
        conclusions.append("分支误预测率较高，控制流可能是主要瓶颈之一。")
    if l1d_miss is not None and instructions is not None:
        l1d_mpki = mpki(l1d_miss, instructions)
        if l1d_mpki is not None and l1d_mpki > 5.0:
            conclusions.append("L1D MPKI 偏高，数据局部性可能不足。")
    if llc_miss is not None and instructions is not None:
        llc_mpki = mpki(llc_miss, instructions)
        if llc_mpki is not None and llc_mpki > 1.0:
            conclusions.append("LLC MPKI 偏高，可能存在较多跨层访问。")
    if not conclusions:
        conclusions.append("指标未显示明显异常，后续可结合火焰图或采样剖析进一步确认热点。")

    system_info = Path(args.system).read_text().strip()
    bench_out = Path(args.bench).read_text().strip()

    if args.workload:
        workload_lines = [line.strip() for line in args.workload.split("\n") if line.strip()]
    else:
        workload_lines = [
            f"add/delete 各占一半，trade 占比 {args.trade}%",
            f"每次迭代操作数: {args.ops}",
            "预热订单数: 20000，最大活跃订单数: 50000，价位数: 2000",
        ]

    out = Path(args.out)
    out.write_text(
        """# OrderBook 性能指标案例（对标 4-11 风格）

## 测试环境

```
{system_info}
```

## 工作负载

{workload_bullets}

## 基准输出

```
{bench_out}
```

## 性能指标表

| 指标 | 值 | 说明 |
|---|---:|---|
{metric_rows}

## 结论（草稿）

{conclusions}
""".format(
            system_info=system_info,
            bench_out=bench_out,
            workload_bullets="\n".join(f"- {line}" for line in workload_lines),
            metric_rows="\n".join(
                f"| {name} | {value} | {note} |" for name, value, note in metrics
            ),
            conclusions="\n".join(f"- {c}" for c in conclusions),
        )
    )


if __name__ == "__main__":
    main()
