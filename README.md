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

## Measured Results (Intel Raptor Lake, GCC 15, size 500000)

The following measurements were collected on a `13th Gen Intel(R) Core(TM) i9-13900K` with `g++ (Debian 15.2.0-13) 15.2.0`, rerun with `--size 500000`.

Command template:

```bash
numactl --physcpubind=0-15 perf stat \
  -e branch-misses,cache-misses,instructions,cycles \
  ./binary_search_bench --size 500000 --queries 20000000 --mode <mode> --search <search>
```

Derived metrics:

- `IPC = instructions / cycles`
- `Branch MPKI = branch-misses * 1000 / instructions`
- `Cache MPKI = cache-misses * 1000 / instructions`

| mode | search | elapsed_seconds | ns/query | cycles | IPC | Branch MPKI | Cache MPKI |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `random-miss` | `branchy` | 1.44532 | 72.2658 | 8,545,203,518 | 0.61 | 40.48 | 0.67 |
| `random-miss` | `cmov-cpp` | 0.491703 | 24.5851 | 3,047,328,271 | 1.33 | 2.49 | 0.82 |
| `random-miss` | `cmov-asm` | 0.583478 | 29.1739 | 3,618,434,287 | 1.61 | 0.19 | 0.56 |
| `fixed-high-miss` | `branchy` | 0.302998 | 15.1499 | 1,858,684,438 | 2.19 | 3.49 | 0.69 |
| `fixed-high-miss` | `cmov-cpp` | 0.237739 | 11.8870 | 1,498,757,855 | 2.11 | 0.01 | 0.88 |
| `fixed-high-miss` | `cmov-asm` | 0.327229 | 16.3615 | 2,016,563,933 | 2.23 | 0.01 | 0.63 |
| `sequential-miss` | `branchy` | 0.596700 | 29.8350 | 3,739,854,664 | 1.14 | 19.00 | 0.54 |
| `sequential-miss` | `cmov-cpp` | 0.234867 | 11.7433 | 1,642,764,007 | 1.91 | 0.01 | 0.72 |
| `sequential-miss` | `cmov-asm` | 0.387705 | 19.3852 | 2,537,434,026 | 1.93 | 0.20 | 0.49 |

Observations:

- `cmov-cpp` is the fastest variant in all three query modes.
- The biggest branch-prediction win is `random-miss`: `Branch MPKI` drops from `40.48` in `branchy` to `2.49` in `cmov-cpp`.
- `cmov-asm` drives `Branch MPKI` even lower, but it still loses to `cmov-cpp` on elapsed time.

## Measured Results (AMD Zen 5, size 500000)

The following measurements were collected on an `AMD RYZEN AI MAX+ 395 w/ Radeon 8060S` using the same benchmark binary, without `numactl`.

Command template:

```bash
perf stat -e branch-misses,cache-misses,instructions,cycles \
  ./binary_search_bench --size 500000 --queries 20000000 --mode <mode> --search <search>
```

| mode | search | elapsed_seconds | ns/query | cycles | IPC | Branch MPKI | Cache MPKI |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `random-miss` | `branchy` | 1.79508 | 89.7541 | 9,113,210,091 | 0.57 | 40.43 | 18.89 |
| `random-miss` | `cmov-cpp` | 0.738557 | 36.9279 | 3,918,421,041 | 1.02 | 2.55 | 11.20 |
| `random-miss` | `cmov-asm` | 0.957382 | 47.8691 | 5,019,264,184 | 1.14 | 0.21 | 5.29 |
| `fixed-high-miss` | `branchy` | 0.169506 | 8.47531 | 886,938,583 | 4.49 | 0.01 | 0.06 |
| `fixed-high-miss` | `cmov-cpp` | 0.296316 | 14.8158 | 1,539,239,641 | 2.00 | 0.02 | 0.14 |
| `fixed-high-miss` | `cmov-asm` | 0.549202 | 27.4601 | 2,773,582,248 | 1.59 | 0.01 | 0.12 |
| `sequential-miss` | `branchy` | 0.654119 | 32.7060 | 3,414,097,611 | 1.23 | 16.41 | 0.15 |
| `sequential-miss` | `cmov-cpp` | 0.298267 | 14.9133 | 1,700,677,006 | 1.80 | 0.01 | 0.10 |
| `sequential-miss` | `cmov-asm` | 0.595988 | 29.7994 | 3,186,921,626 | 1.52 | 0.21 | 0.06 |

Observations:

- `cmov-cpp` is still the fastest variant for `random-miss` and `sequential-miss`.
- `branchy` wins the highly predictable `fixed-high-miss` case on this machine and data size.
- The remote `random-miss` run shows the same branch-prediction pattern: `Branch MPKI` falls from `40.43` in `branchy` to `2.55` in `cmov-cpp`.

## What To Expect

You should usually see these trends on modern out-of-order CPUs:

- `random-miss` has a noticeably higher `Branch MPKI`.
- `cmov-cpp` can reduce `Branch MPKI` for `random-miss` without inline assembly. On the `Intel Core i9-13900K` + `GCC 15` measurements above, it is the fastest variant in all three modes.
- `cmov-asm` is useful as a reference if you want to compare compiler-generated `cmov` against an explicit x86 implementation.
- `random-miss` takes more `cycles` per query.
- `fixed-high-miss` often shows a lower `Branch MPKI` because the branch pattern is nearly identical each time, and on some CPUs that predictability can make the original branchy search faster than a `cmov` rewrite.
- `Cache MPKI` may also move because the query pattern changes which tree nodes stay hot, but the branch behavior is the primary signal here.

To estimate the cost per query, divide the total cycle count by the number of queries. The branch misprediction penalty is often visible as a higher cycle budget even though the algorithmic complexity is still $O(\log N)$.

## Notes

- `hits` should remain `0` in the miss-driven modes above.
- If `perf stat` reports a permissions error, check the current `kernel.perf_event_paranoid` setting on the machine.