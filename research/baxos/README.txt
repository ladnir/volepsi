Baxos optimization pass -- 2026-11-15
===================================

Result
------
Peach, one thread, 2^20 entries (final repeat, medians after warmup):
  operation       preserved baseline      optimized      reduction
  encode               104.71 ms           78.19 ms         25.3%
  decode                31.98 ms           21.94 ms         31.4%

An earlier run of the same qualified executable measured 78.17 / 21.90
ms. Recompiling changed code layout and produced 78.44 / 22.95 ms in
an intermediate build; retain the raw data rather than attributing every
small timing difference to a specific optimization.

All 21 registered Paxos/Baxos checks pass natively, with ASan/UBSan
(halt on first undefined behavior), and with AVX2 and forced block
arithmetic backends. Tests include GF128/binary, vector/matrix payloads,
forced gaps, randomized/nonrandomized encoding, multiple threads, tail
batches, scalar nonmember checks, add-to-decode, and parameter failures.
The new arithmetic checks compare all SIMD remainders against C++ % and
Horner results against an independent field-power sum at degrees 1, 2, 3, 7, 17, and 40.

Final single-thread encoded tables plus mixed member/nonmember outputs
are byte-identical to the preserved binary at 2^16 and 2^20 entries,
using different matrix/free-cell seeds. Differential hashes, source
manifests, raw CSVs, profiles, and validation logs are under
results/peach-20261115. Full source/binary snapshots and exploratory logs
are archived in out/baxos-opt-20261115/results.tgz. Each manifest identifies
the exact sources used for that build.

Scope
-----
Production Paxos/Baxos code, with unchanged row hashes, table dimensions,
field polynomial, bin assignment, pivot order, and single-thread PRNG
consumption. The Ribbon prototype is separate and was not changed.

Changes
-------
- Remove the encoder's unused input-index mapping and its scattered writes.
- Keep 32-way remainder calculations in SIMD registers. Power-of-two
  divisors use an exact mask; general divisors retain libdivide quotients.
- Evaluate the dense polynomial with Horner's rule: d field multiplies
  instead of 2*d-1. Eight rows run together using AVX-512 VPCLMUL, AVX2
  VPCLMUL, or the existing block multiplication backend. Custom helpers
  and matrix-valued elements retain the original generic implementation.
- Track the XOR of active row indices per column. Weight-one columns can
  select their sole row without scanning stale adjacency entries. Keep
  the existing weight-set order and gap handling.
- Reuse Paxos's owned arena when its capacity suffices.

Correctness fixes found during validation
----------------------------------------
Parallel randomized encoding previously passed the same mutable PRNG to
all workers. Workers now receive separately seeded PRNGs before launch.
Single-thread consumption is unchanged. Parallel randomized tables and
parent-PRNG consumption change from the previous racy implementation;
for a fixed thread count and seed they are now independent of scheduling.
The regression checks repeated encodes, parent state, and decoded values.

The scalar hash row builder read a u64 at a four-byte offset through a
u64 pointer. Fixed-size memcpy loads retain the same bytes without the
alignment/aliasing violation. Scratch arenas now place pointer-aligned
span objects before narrower index arrays. This removes misalignment
for odd row counts without adding padding or changing table contents.

Benchmark method
----------------
Peach Ryzen 7950X, one thread pinned to CPU 2, GCC 15.2, -O3 -march=native,
2^20 keys, 128-bit values, bin target 2^14, weight 3, SSP 40, GF128 dense
columns. The resulting table has 1,411,712 cells and two dense columns
per bin. Fresh allocation and randomized free cells on every encode;
queries are half members and half independent keys. Eight trials, with
the first excluded from the reported median. All benchmarks, builds,
and tests take the shared volepsi, hypercat, and cat-blst locks.

Timing includes initialization and output allocation for encoding, and
all hashing, binning, arithmetic, scattering, and scratch allocation /
destruction. Verification is outside timed regions. These are standalone
OKVS measurements, not PSI totals. No huge-page advice was added.

Baseline executable and source snapshots are preserved. perf samples
identified dense decode, modular row construction, binning, and sparse
triangulation as the major costs. Raw profiles include setup overhead
and are diagnostic, not the source of reported wall timings.

Reproduction
------------
Working remote directory: /tmp/baxos-opt-20261115
Dependencies: /tmp/volepsi-chosenbaa30-20261115
Copy volePSI/, tests/Paxos_Tests.*, bench.cpp, tests.cpp, and peach.py to
that directory before running the following commands. The build links
the existing dependencies while compiling the edited template headers.

  python3 peach.py build --exe qualified
  python3 peach.py bench --exe qualified --out qualified-after
  python3 peach.py test --exe final-native-tests
  python3 peach.py sanitize --exe final-sanitize-tests
  python3 peach.py test --exe avx2-tests --isa avx2
  python3 peach.py test --exe scalar-tests --isa scalar

The avx2 build disables AVX-512 and exercises two-lane VPCLMUL plus the
AVX2 remainder product. The scalar build forces the eight independent block evaluators
(128-bit PCLMUL on Peach). cryptoTools itself retains its configured
VPCLMUL requirements; the override affects only the new Baxos kernel. These
checks do not substitute for native ARM measurements.

Next step
---------
Rerun single-thread semi-honest PSI on Peach to measure the end-to-end gain.
