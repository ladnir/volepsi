Ribbon OKVS optimization -- 2026-11-15
=====================================

Result
------
Encoding is 41-43% faster than the first Ribbon prototype. Full decoding
is approximately unchanged. The matrix distribution and output format are
unchanged. Differential tests produce byte-for-byte identical tables,
including randomized free cells.

Peach, one thread pinned to CPU 2, 2^20 keys, 128-bit values:

  Profile                Previous encode    New encode    New decode
  width 128, 1.25*n          76.10 ms          44.88 ms      26.09 ms
  width 112, 1.30*n          74.32 ms          43.67 ms      24.75 ms
  width  80, 1.40*n          71.77 ms          40.75 ms      23.37 ms

Previous decode medians were 26.59, 24.93, and 23.28 ms respectively.
The preserved Baxos executable remains approximately 104 ms to encode
and 34-36 ms to decode. These are standalone OKVS timings, not PSI totals.

The defaults request transparent huge pages for large owned buffers.
Peach has THP configured for madvise; the live width-128 output buffer
reported 20480 KiB of huge pages. No global system setting was changed.
Correctness does not depend on acceptance of the advice. With huge-page
advice disabled, encode/decode medians are:
  width 128: 62.56 / 26.91 ms
  width 112: 61.29 / 25.80 ms
  width  80: 58.95 / 23.47 ms
The larger gain on Peach includes reduced page-fault and translation
costs; it should not be attributed entirely to arithmetic improvements.

Measurement conditions
----------------------
All benchmarks ran sequentially under shared volePSI, hypercat, and
cat-blst locks. Builds and tests now acquire those locks too. Final
figures are medians of seven trials after one warmup, using GCC 15.2,
-O3 and -march=native. Output and workspace are freshly allocated on
every encode. Totals include hashing, sorting, randomization, solving,
and workspace destruction. Decode includes restoring query order.

Inputs are fixed random keys and values. Matrix and free-cell seeds
change each trial. Queries are half members and half independent keys.
Every encode is checked on all members outside the timed region. Timed
decodes check all member answers and sampled scalar nonmember answers.
Only the selected sorted decoder runs in each final-confirmation process.
Baxos runs before and after each sweep using its preserved executable.
Recompiling the shared harness was observed to change Baxos encode
performance despite unchanged Baxos source, so that was not used as the
final baseline.

Selected changes
----------------
- Fuse coefficient generation with elimination. Retain only normalized
  masks in row order and keep each pivot value in the output table.
  At width 128, per-row scratch shrinks from 32 to 16 bytes.
- Avoid initializing temporary arrays that will be overwritten. Random
  output cells and padding are still explicitly initialized.
- Align large buffers and request transparent huge pages on Linux.
  The ordinary-page configuration is also tested and measured.
- Expand masks with PDEP and use masked AVX-512 XORs with memory operands.
  Back substitution uses exact-width 16-byte loads for its first four
  cells, which can forward recent stores.
- Count both radix digits in one scan with 32-bit histograms, preserving
  stable ordering, including the original order of equal-start rows.

Defaults: RIBBON_DOT=2, RIBBON_HUGE_PAGES=1, RIBBON_FORWARDING=1,
RIBBON_RADIX_BITS=11, RIBBON_BATCH=8, RIBBON_DIRECT_PIVOTS=0.
Larger batches, direct column-indexed masks, a smaller radix, and an
exact reciprocal remainder calculation did not improve the selected
workload. The reciprocal calculation was removed; the other kernel
choices remain available for controlled experiments.

Width-128 stage medians, milliseconds:
  initialization and random output                 2.31
  hash starts                                      1.90
  sort indices                                     4.59
  fused coefficient generation and elimination     22.84
  back substitution                               13.24
  encode wall total                               44.88
  decode allocation                                0.01
  decode start hashing                             2.09
  decode sorting                                   4.51
  coefficient hashing, evaluation, output scatter  19.51
  decode wall total                               26.09
Stage medians need not sum exactly to the median total. Fused elimination
includes first-touch memory costs. Decode still has substantial sorting
and scattered input/output costs.

Validation and scope
--------------------
Native and ASan/UBSan tests passed. Coverage includes partial batches,
partial words, scalar/SIMD agreement, duplicate-row rank failure, and
large allocations crossing the huge-page threshold. Differential tests
compare every output cell with the preserved encoder at widths 80, 112,
128, and 192 and n=17, 513, and 131075. They pass natively and with
ASan/UBSan. All large benchmark correctness checks passed.

proof.txt and parameters.json retain the one-attempt rank bounds under
ideal independent random starts and coefficient bits:
  width 128, 25% overhead: < 2^-46.97448
  width 112, 30% overhead: < 2^-48.76238
  width  80, 40% overhead: < 2^-42.67122
These bounds do not complete the AES-instantiation or adaptive/malicious
PSI/OPRF audit. This is an AVX-512 research OKVS over 128-bit values,
separate from the ChosenBAA30 VOLE code.

Reproduction and artifacts
--------------------------
Remote directory: /tmp/ribbon-okvs-20261115/research/ribbon
Dependencies: /tmp/volepsi-chosenbaa30-20261115

  python3 peach.py build --exe qualified --wait-lock 60
  python3 peach.py test --exe qualified --wait-lock 60
  python3 peach.py sanitize --wait-lock 60
  python3 peach.py differential-sanitize --wait-lock 60
  python3 peach.py bench --exe qualified --quick --sorted-only --trials 8 --out optimized-final --wait-lock 60

Build the fallback with --huge-pages 0 and a separate --exe name.
The baseline executable is optimization-baseline/bench. Differential
tests verify the preserved header's SHA-256 before generating its copy.
Each build saves source snapshots, its command, and source hashes.
Metadata records the tested executable and preserved baseline hashes.

results/peach-20261115-optimized contains final CSVs, stage summaries,
metadata, huge-page observations, and validation results. Those manifests identify the encoder-optimization source snapshots;
the batched-decoder measurements below identify the current sources.
results/peach-20261115 retains the initial prototype measurements.
Exploration logs and source snapshots are archived locally in
out/ribbon-optimize-20261115/results.tgz; the initial prototype archive
is out/ribbon-review-20261115/results.tgz.

Batched decoding (2026-11-15, subsequent pass)
-------------------------------------------
Order::shared evaluates eight nearby sorted queries together with AVX-512
GFNI and VBMI. Every eight source table cells are converted into sixteen
8x8 binary matrices. Two GFNI operations apply those matrices to all eight
queries at once. The row coefficients and decoded values are unchanged.

A mirrored rolling cache retains converted cells between neighboring
batches. At the primary widths it occupies 4 or 8 KiB on the stack, with
no extra table allocation. Each source cell is converted at most once
per decode. Queries too far apart, partial batches, and widths below 32 use the
previous SIMD dot product. The narrow-width fallback keeps the wider
GFNI loads within the existing padding contract. Builds without GFNI/VBMI use the previous sorted path.
This remains an AVX-512 research prototype, not a portable PSI backend.

Peach, one thread pinned to CPU 2, 2^20 keys and queries, identical seeds
and workload, eight trials with trial zero excluded; medians in ms:

  width / overhead     preserved sorted    new shared    reduction
  128 / 25%                   24.86            21.14         14.9%
  112 / 30%                   24.77            21.72         12.3%
   80 / 40%                   23.23            21.28          8.4%

All times include allocation/destruction, start hashing, sorting,
coefficient hashing, table conversion, evaluation, and output scatter.
No precomputed table or prepared query state is excluded from timing.
Before the final narrow-width guard, the old sorted path in the same
executable measured 25.10, 24.79, and 23.12 ms. The shared path in that
build measured 21.06, 21.58, and 20.53 ms. The table above uses the final
qualified build; earlier compiler/layout-sensitive results are retained
in the raw data. Encoding is unchanged and remains about
45, 44, and 41 ms. Runs, builds, and checks were serialized using all
three shared benchmark locks. Huge-page hints were enabled as before.

The shared evaluation stage (including coefficient hashing, conversion,
and scatter) measured 16.22, 15.13, and 14.71 ms. Width-128 sorting now
measured 2.54 ms instead of the prior pass's 4.51 ms; the preserved binary
shows the same allocation-sensitive change, so the comparison above uses
fresh baseline measurements rather than claiming that as a decoder win.

Rejected experiments: ordinary masked-XOR groups of two, four, or eight
queries; additional accumulators; quarter-sized query tiles; and a full
converted table. The rolling GFNI implementation was the consistent win.
The old decoder modes remain available; rejected new paths were removed.

Reproduce the selected path:
  python3 peach.py build --exe batched-qualified --wait-lock 60
  python3 peach.py test --exe batched-qualified --wait-lock 60
  python3 peach.py bench --exe batched-qualified --quick --ribbon-only --shared-only --trials 8 --out batched-qualified-results --wait-lock 60
  python3 peach.py bench --exe batched-final --quick --ribbon-only --sorted-only --trials 8 --out batched-same-binary-sorted --wait-lock 60

Native tests cover all coefficient positions, offsets 0 through 32,
partial words/batches, sparse fallback, members, and scalar nonmembers.
Differential tests compare every encoded cell and every decoded member
and independent nonmember against the preserved implementation, including
262147-element inputs that exercise many ring wraps and huge buffers.
Native and ASan/UBSan tests, including the differential comparisons,
passed. The AVX-512 build without GFNI/VBMI is checked separately with
--no-gfni. Validation logs and raw benchmark data are under
results/peach-20261115-batched. Source snapshots and exploration results
are archived in out/ribbon-batched-20261115/results.tgz.

Recommended next step
---------------------
Reduce the remaining coefficient-hashing and output-scatter costs.
Keep PSI integration separate until its protocol proof obligations
are audited.
