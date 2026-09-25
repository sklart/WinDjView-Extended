# Multi-worker tile performance (Phase 8C)

`tools\tests\run-tile-performance-benchmark.cmd` builds Native Release x64
with the normal production flags, then runs `tile_performance_benchmark.cpp`
against the pinned real-DjVu corpus. Its explicit worker limit (1, 2, or 4)
is test-only; ordinary `CRenderThread` callers retain the hardware-based
production choice. No renderer, scheduler, cache, or golden baseline changes
are made by the benchmark.

Each scenario uses one warm-up and five measured requests per worker count.
The same worker and decoded source are reused within those six requests.
Full-page reference rendering and pixel validation run outside the timed
interval. `page_rendered_ms` runs from the first request to the final
`PAGE_RENDERED` callback; `total_ms` ends when the queue and active tile work
are drained. Both use `QueryPerformanceCounter`. The benchmark rejects any
pixel mismatch, duplicate/unexpected publication, missing result, or worker
count above its configured limit. Timing has no pass/fail threshold.

The Native Release x64 CI artifact `tile-performance-native-release-x64`
contains `samples.csv` (all measured runs), `summary.csv` (median/min/max for
time, completed tiles, peak active workers, fallbacks and rejected stale
results), `comparisons.csv` (1→2, 1→4, 2→4 ratios), `metadata.txt`, and the
console log. The rapid distant case uses pages 0→11→6 of the 12-page
`watchmaker.djvu`; an active tile is held while the two subsequent requests
and reconciliation are issued. The sequential case renders both pages of
`cable_1973_100133.djvu`. All target bitmaps are 3072×3072 pixels.

## Local reference result

2026-09-25, Intel Core i5-13400F (10 cores/16 logical processors), VS 2022
v143/MSVC 14.44 Native Release x64. Times are milliseconds, shown as
median [min–max] from the second of three successful local full benchmark runs.
Ratios compare median `total_ms`; values above 1 are faster.

| Scenario | 1 worker | 2 workers | 4 workers | 1→2 | 1→4 | 2→4 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| B&W large | 24.861 [24.758–25.459] | 14.412 [13.946–15.509] | 8.857 [8.469–10.402] | 1.725× | 2.807× | 1.627× |
| Large scanned (fallback) | 243.285 [240.471–249.880] | 235.617 [233.030–238.707] | 246.573 [237.650–249.978] | 1.033× | 0.987× | 0.956× |
| Rotation | 36.283 [35.364–36.525] | 20.506 [20.244–20.798] | 12.502 [11.694–13.150] | 1.769× | 2.902× | 1.640× |
| Crop | 35.818 [34.716–40.269] | 19.638 [19.107–21.327] | 11.288 [10.120–12.197] | 1.824× | 3.173× | 1.740× |
| Display adjustments | 20.018 [19.671–20.533] | 11.158 [10.767–11.512] | 7.115 [6.108–7.386] | 1.794× | 2.813× | 1.568× |
| Sequential navigation | 39.780 [38.959–39.859] | 21.845 [21.395–22.177] | 13.641 [12.879–13.764] | 1.821× | 2.916× | 1.601× |
| Rapid distant changes | 80.817 [73.339–85.304] | 78.484 [76.594–79.949] | 80.380 [75.616–80.959] | 1.030× | 1.005× | 0.976× |

The earlier successful run showed the same qualitative result: B&W 1→4
was 3.285×, rotation 3.069×, crop 3.048×, adjustments 2.751×, and the
fallback/distant-change cases stayed near 1×. These are host-specific
observations, not a performance guarantee for other machines or documents.

## Decision

Keep the current complete-page publication and bounded tile workers for now.
The measured tiled B&W workloads benefit materially, while the full-page
fallback and stale-navigation workloads do not gain from more workers.
Neither progressive repaint nor a tile cache is justified by this timing
data alone: progressive repaint would need a separate perceived-latency
evaluation, and tile-cache reuse would need realistic repeated pan/zoom
traces plus a memory budget. Both remain separate future decisions.

## Phase 8D stabilization

The blocking Native Release x64 `tile_stress_regression` drives the real
`CRenderThread` and shared scheduler through repeated A→B→A→C→A replacements,
viewport-style reconciliation, sequential page changes, zoom, rotation, crop,
display-mode and adjustment changes, and document close/reopen with active
workers. Every completed request must publish exactly once with its current
identity. The test checks that active workers, queued jobs and live batches
return to zero, batch creation/destruction balances, stale tile results are
rejected, and no batch starts more than one full-page fallback. It reports
configured/peak workers, completed/rejected tiles, fallbacks and batch
lifecycle counts. Process private bytes after warm-up, peak and final steady
state are reported. A deliberately generous 64 MiB post-warm-up ceiling and
handle-count check after the first close catch sustained growth across repeated
document reopens; first-use runtime initialization and small allocator
retention are not treated as leaks by themselves.

The known full-page fallback cases remain layered color region differences,
whole-source PnmScaleFixed scaling, unsupported regions, and tile render
failures. Thumbnail, print and export rendering stays full-page. Phases 8A–8D
cover independent region rasterization, bounded parallel scheduling,
repeatable performance measurement and runtime lifecycle hardening. Progressive
repaint and a tile cache remain intentionally deferred pending separate
user-perceived latency and repeated-pan evidence with a memory budget.
