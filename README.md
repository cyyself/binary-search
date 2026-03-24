# Binary Search Branch Prediction Benchmark

This repository contains a small C++ benchmark that characterizes binary search under different query patterns and search implementations.

The sorted array is built from even integers only, so every odd target is guaranteed to be absent. That makes it easy to generate misses while controlling the branch behavior.

## Why This Shows a CPU Bottleneck

Binary search performs about $\log_2(N)$ comparisons per query. With random missing targets, the key branch inside the search changes direction in a hard-to-predict way at each tree level. When the branch predictor guesses wrong, the CPU has to flush speculative work and refill the pipeline, which wastes execution slots and increases cycles per search.

In contrast, a predictable miss such as a value above the largest element follows nearly the same branch pattern on every query, so the predictor converges quickly and the core retires the work with fewer front-end disruptions.

## Files

- `main.cpp`: benchmark implementation and argument parsing
- `Makefile`: optimized build target

## Build

```bash
make
```

## Run

The default configuration uses 1,000,000 sorted integers and 20,000,000 queries.

```bash
./binary_search_bench
```

Useful modes:

- `random-miss`: random odd targets across the array domain
- `fixed-high-miss`: one missing value above the entire array, repeated
- `sequential-miss`: ascending odd targets, wrapping at the end

Useful search kinds:

- `branchy`: original `if/else` update in the hot loop
- `cmov-cpp`: pure C++ lower-bound style rewrite that GCC 15 turns into scalar `cmov` in the inner loop
- `cmov`: alias for `cmov-cpp`
- `cmov-asm`: explicit x86 `cmov` bound updates, kept as a reference implementation

Examples:

```bash
./binary_search_bench --size 1000000 --queries 20000000 --mode random-miss
./binary_search_bench --size 1000000 --queries 20000000 --mode fixed-high-miss
./binary_search_bench --size 1000000 --queries 20000000 --mode random-miss --search cmov-cpp
./binary_search_bench --size 1000000 --queries 20000000 --mode random-miss --search cmov-asm
```

## Measure With perf

For a stable comparison, pin the benchmark to one core and collect the same counters for both modes.

```bash
taskset -c 0 perf stat \
  -e cycles,instructions,branches,branch-misses,cache-references,cache-misses \
  ./binary_search_bench --size 1000000 --queries 20000000 --mode fixed-high-miss --search branchy
```

```bash
taskset -c 0 perf stat \
  -e cycles,instructions,branches,branch-misses,cache-references,cache-misses \
  ./binary_search_bench --size 1000000 --queries 20000000 --mode random-miss --search branchy
```

```bash
taskset -c 0 perf stat \
  -e cycles,instructions,branches,branch-misses,cache-references,cache-misses \
  ./binary_search_bench --size 1000000 --queries 20000000 --mode random-miss --search cmov-cpp
```

```bash
taskset -c 0 perf stat \
  -e cycles,instructions,branches,branch-misses,cache-references,cache-misses \
  ./binary_search_bench --size 1000000 --queries 20000000 --mode random-miss --search cmov-asm
```

If you want less noise, run multiple repetitions:

```bash
taskset -c 0 perf stat -r 5 \
  -e cycles,instructions,branches,branch-misses,cache-references,cache-misses \
  ./binary_search_bench --size 1000000 --queries 20000000 --mode random-miss
```

## What To Expect

You should usually see these trends on modern out-of-order CPUs:

- `random-miss` has a noticeably higher `branch-misses` count and miss ratio.
- `cmov-cpp` can reduce `branch-misses` for `random-miss` without inline assembly, but it may increase instruction count or hurt predictable cases.
- `cmov-asm` is useful as a reference if you want to compare compiler-generated `cmov` against an explicit x86 implementation.
- `random-miss` takes more `cycles` per query.
- `fixed-high-miss` often shows a lower miss ratio because the branch pattern is nearly identical each time.
- `cache-misses` may also move because the query pattern changes which tree nodes stay hot, but the branch counters are the primary signal here.

To estimate the cost per query, divide the total cycle count by the number of queries. The branch misprediction penalty is often visible as a higher cycle budget even though the algorithmic complexity is still $O(\log N)$.

## Notes

- `hits` should remain `0` in the miss-driven modes above.
- If `perf stat` reports a permissions error, check the current `kernel.perf_event_paranoid` setting on the machine.