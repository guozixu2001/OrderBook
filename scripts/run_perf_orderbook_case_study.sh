#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-perf"
OUT_DIR="${ROOT_DIR}/perf-orderbook"

OPS_PER_ITER="${1:-50000}"
TRADE_PCT="${2:-10}"

BENCH_FILTER="BM_OrderBookWorkload/ops:${OPS_PER_ITER}/trade_pct:${TRADE_PCT}"

EVENTS="cycles,instructions,branches,branch-misses,cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses,L1-icache-load-misses,LLC-loads,LLC-load-misses,dTLB-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses"

mkdir -p "${BUILD_DIR}" "${OUT_DIR}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" -j

BENCH_BIN="${BUILD_DIR}/benchmarks/benchmarks"

SYSINFO_FILE="${OUT_DIR}/system.txt"
PERF_CSV="${OUT_DIR}/perf-stat.csv"
BENCH_OUT="${OUT_DIR}/bench_output.txt"
REPORT_MD="${OUT_DIR}/orderbook_case_study.md"

{
  echo "Date: $(date -u +"%Y-%m-%dT%H:%M:%SZ")"
  uname -a
  echo "---- lscpu ----"
  lscpu
  echo "---- meminfo ----"
  grep -E "MemTotal|MemFree|MemAvailable" /proc/meminfo
} > "${SYSINFO_FILE}"

"${BENCH_BIN}" \
  --benchmark_filter="${BENCH_FILTER}" \
  --benchmark_min_time=1s \
  --benchmark_repetitions=5 \
  --benchmark_report_aggregates_only=true \
  --benchmark_format=console \
  > "${BENCH_OUT}"

perf stat -x, -e "${EVENTS}" -- \
  "${BENCH_BIN}" \
  --benchmark_filter="${BENCH_FILTER}" \
  --benchmark_min_time=1s \
  --benchmark_repetitions=5 \
  --benchmark_report_aggregates_only=true \
  --benchmark_format=console \
  2> "${PERF_CSV}"

python3 "${ROOT_DIR}/scripts/parse_perf_orderbook.py" \
  --perf "${PERF_CSV}" \
  --system "${SYSINFO_FILE}" \
  --bench "${BENCH_OUT}" \
  --ops "${OPS_PER_ITER}" \
  --trade "${TRADE_PCT}" \
  --out "${REPORT_MD}"

echo "Wrote ${REPORT_MD}"
