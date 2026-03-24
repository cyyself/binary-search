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

## Measured Results (Intel Raptor Lake, GCC 15, size 5000000)

The following measurements were collected on a `13th Gen Intel(R) Core(TM) i9-13900K` with `g++ (Debian 15.2.0-13) 15.2.0`, rerun with `--size 5000000` (about 19 MiB of `int` data) to exercise an L3-scale working set.

Command template:

```bash
numactl --physcpubind=0-15 perf stat \
  -e branch-misses,cache-misses,instructions,cycles \
  ./binary_search_bench --size 5000000 --queries 20000000 --mode <mode> --search <search>
```

Derived metrics:

- `IPC = instructions / cycles`
- `Branch MPKI = branch-misses * 1000 / instructions`
- `Cache MPKI = cache-misses * 1000 / instructions`

| mode | search | elapsed_seconds | ns/query | cycles | IPC | Branch MPKI | Cache MPKI |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `random-miss` | `branchy` | 2.26188 | 113.094 | 13,424,559,037 | 0.44 | 41.03 | 1.39 |
| `random-miss` | `cmov-cpp` | 1.27948 | 63.9741 | 7,675,841,111 | 0.57 | 0.83 | 1.77 |
| `random-miss` | `cmov-asm` | 1.44535 | 72.2677 | 8,706,274,132 | 0.76 | 1.12 | 1.02 |
| `fixed-high-miss` | `branchy` | 0.340742 | 17.0371 | 2,157,424,934 | 2.29 | 1.09 | 0.69 |
| `fixed-high-miss` | `cmov-cpp` | 0.290021 | 14.5011 | 1,864,398,204 | 1.83 | 0.01 | 0.98 |
| `fixed-high-miss` | `cmov-asm` | 0.420014 | 21.0007 | 2,616,254,078 | 2.08 | 0.01 | 0.63 |
| `sequential-miss` | `branchy` | 0.646554 | 32.3277 | 4,095,844,809 | 1.21 | 16.82 | 0.69 |
| `sequential-miss` | `cmov-cpp` | 0.381637 | 19.0818 | 2,572,184,963 | 1.34 | 0.01 | 0.85 |
| `sequential-miss` | `cmov-asm` | 0.468847 | 23.4424 | 3,072,468,071 | 1.85 | 0.18 | 0.55 |

Observations:

- `cmov-cpp` is the fastest variant in all three query modes.
- The biggest branch-prediction win is `random-miss`: `Branch MPKI` drops from `41.03` in `branchy` to `0.83` in `cmov-cpp`.
- At this larger working set, `Cache MPKI` rises compared with the smaller run, but `cmov-cpp` still wins on elapsed time across all three Intel query modes.

## Measured Results (AMD Zen 5, size 5000000)

The following measurements were collected on an `AMD RYZEN AI MAX+ 395 w/ Radeon 8060S` using the same benchmark binary, without `numactl`, at `--size 5000000` (about 19 MiB of `int` data).

Command template:

```bash
perf stat -e branch-misses,cache-misses,instructions,cycles \
  ./binary_search_bench --size 5000000 --queries 20000000 --mode <mode> --search <search>
```

| mode | search | elapsed_seconds | ns/query | cycles | IPC | Branch MPKI | Cache MPKI |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `random-miss` | `branchy` | 2.26249 | 113.125 | 11,793,795,123 | 0.49 | 41.73 | 71.98 |
| `random-miss` | `cmov-cpp` | 1.79 | 89.5001 | 9,370,746,795 | 0.46 | 0.97 | 64.04 |
| `random-miss` | `cmov-asm` | 1.65066 | 82.5328 | 8,685,400,206 | 0.75 | 1.11 | 18.16 |
| `fixed-high-miss` | `branchy` | 0.219539 | 10.977 | 1,147,568,894 | 4.14 | 0.01 | 0.10 |
| `fixed-high-miss` | `cmov-cpp` | 0.358964 | 17.9482 | 1,887,543,297 | 1.74 | 0.01 | 0.15 |
| `fixed-high-miss` | `cmov-asm` | 0.699365 | 34.9683 | 3,634,908,680 | 1.46 | 0.01 | 0.07 |
| `sequential-miss` | `branchy` | 0.658906 | 32.9453 | 3,552,487,158 | 1.36 | 13.72 | 0.11 |
| `sequential-miss` | `cmov-cpp` | 0.446992 | 22.3496 | 2,477,357,077 | 1.34 | 0.01 | 0.19 |
| `sequential-miss` | `cmov-asm` | 0.746156 | 37.3078 | 4,010,348,617 | 1.39 | 0.35 | 0.08 |

Observations:

- No single variant wins every workload on this larger Zen 5 run.
- `cmov-asm` is fastest for `random-miss`, `branchy` is fastest for `fixed-high-miss`, and `cmov-cpp` is fastest for `sequential-miss`.
- `branchy` wins the highly predictable `fixed-high-miss` case on this machine and data size.
- The remote `random-miss` run still shows the same branch-prediction pattern: `Branch MPKI` falls from `41.73` in `branchy` to `0.97` in `cmov-cpp`, but `cmov-asm` wins elapsed time because it also cuts `Cache MPKI` sharply.

## What To Expect

You should usually see these trends on modern out-of-order CPUs:

- `random-miss` has a noticeably higher `Branch MPKI`.
- `cmov-cpp` can reduce `Branch MPKI` for `random-miss` without inline assembly. On the `Intel Core i9-13900K` + `GCC 15` measurements above, it is the fastest variant in all three modes.
- `cmov-asm` is useful as a reference if you want to compare compiler-generated `cmov` against an explicit x86 implementation.
- `random-miss` takes more `cycles` per query.
- `fixed-high-miss` often shows a lower `Branch MPKI` because the branch pattern is nearly identical each time, and on some CPUs that predictability can make the original branchy search faster than a `cmov` rewrite.
- At larger, L3-scale working sets, `Cache MPKI` can become a deciding factor even when `Branch MPKI` improves.

To estimate the cost per query, divide the total cycle count by the number of queries. The branch misprediction penalty is often visible as a higher cycle budget even though the algorithmic complexity is still $O(\log N)$.

## Notes

- `hits` should remain `0` in the miss-driven modes above.
- If `perf stat` reports a permissions error, check the current `kernel.perf_event_paranoid` setting on the machine.