# Real-world DjVu regression corpus

This directory defines the reproducible input corpus for WinDjView Extended's real-DjVu compatibility regression and separate informational performance baseline. It is not a release asset and does not add rendering or OCR comparison.

The canonical curated source is [`matyushkin/djvu-rs`](https://github.com/matyushkin/djvu-rs), commit [`15f327081b68c46c516fc5189449a787b92c9ebd`](https://github.com/matyushkin/djvu-rs/tree/15f327081b68c46c516fc5189449a787b92c9ebd/tests/corpus), path `tests/corpus`. Every download URL is pinned to that commit; no `main` URL is used.

## Fetch and validate

Windows PowerShell 5.1 and PowerShell 7 are supported. No Python or external PowerShell module is required.

```powershell
pwsh .\tools\tests\corpus\fetch-corpus.ps1
```

To place a disposable local copy elsewhere, use `-OutputDir`. It receives a copy of the canonical manifest on first run.

```powershell
pwsh .\tools\tests\corpus\fetch-corpus.ps1 -OutputDir D:\djvu-corpus
```

If DjVuLibre's `djvudump.exe` is on `PATH`, the script saves a raw dump for each fixture and records observed page counts, chunks, and feature flags. It can also be supplied explicitly:

```powershell
pwsh .\tools\tests\corpus\fetch-corpus.ps1 `
  -DjVuDumpPath "C:\Program Files\DjVuLibre\djvudump.exe"
```

The parser can be tested without DjVuLibre or a downloaded fixture:

```powershell
pwsh .\tools\tests\corpus\fetch-corpus.ps1 -ParserSelfTest
```

After building the selected static `libdjvu` configuration, run the real decode
regression without starting WinDjView's GUI:

```powershell
cmd /d /c .\tools\tests\run-real-djvu-regression.cmd Release x64 native
```

The separate, informational performance baseline uses the same positive
fixtures but is not a correctness regression and has no thresholds. It records
open and first/middle/last-page decode timing with `QueryPerformanceCounter`,
plus the process peak working set. Run it only after a native Release x64 build:

```powershell
cmd /d /c .\tools\tests\run-real-djvu-benchmark.cmd Release x64 native
```

It performs four runs per fixture (run zero is cold-ish), writes min/median/max
to `tools/tests/artifacts/djvu-performance-baseline/`, and emits JSON plus a
readable text report. In addition to individual page timings, the report has a
deterministic sequential-navigation timing for up to three next-page
transitions, so artifacts from before and after prefetch changes can be
compared. It also records a real `DjVuSource`/`CRenderThread` miss-versus-
prefetched-neighbour measurement. GitHub Actions uploads those files only from
native Release x64; the benchmark is informational and cannot mask a
correctness failure.

The runner opens each positive manifest fixture through `DjVuDocument`,
validates its page count, and decodes the first, middle, and last pages as
applicable. It checks text, decoded annotation/hyperlink map areas, and
non-empty bookmark APIs whenever the manifest declares them. Corrupt fixtures
must fail through the helper's controlled error path with exactly the
manifest's `expected_failure` value. The helper emits one machine-readable
result line: `CORPUS_RESULT: PASS` or `CORPUS_RESULT: FAIL <type>`; a success,
crash, timeout, missing line, or wrong failure type is a failure. Each
fixture has its own timeout and process, so one failure does not prevent the
remaining fixtures from reporting their results. The runner also copies a
working fixture to Cyrillic, non-ASCII, and ordinary Unicode paths longer than
260 characters before opening it, without adding a `\\?\` prefix.

`-Force` re-downloads every fixture. Downloads use a `.part` file, validate the `AT&TFORM` magic before promotion, then calculate SHA-256. A bad fixture, checksum mismatch, or known page-count mismatch exits non-zero, while independent fixtures continue to be processed. `SHA256SUMS.txt` is generated in manifest order and per-file manifests are regenerated from the top-level `manifest.json`.

The files under `files/` and raw data under `dumps/` are ignored by Git. The
manifest, SHA list, script, README, and one diagnostic manifest per fixture are
committed. In particular, the 520-page *Pathogenic Bacteria* fixture is always
downloaded locally and is not in the release package. The four negative files
are regenerated from the pinned local positives and their recorded SHA-256
values make that generation reproducible; they are never downloaded or
committed.

## Fixtures and provenance

Actual sizes and SHA-256 values below are from the pinned source. `actual pages` is deliberately `not checked` until `djvudump` is available; expected counts are not presented as observed data.

| ID | Filename | Class | Expected pages | Actual pages | Size (bytes) | License / rights basis | Original source | Validation |
| --- | --- | --- | ---: | --- | ---: | --- | --- | --- |
| watchmaker | `watchmaker.djvu` | color IW44 | 12 | not checked | 183352 | IA public-domain designation; redistribution review required | [Internet Archive](https://archive.org/details/Watchmaker2001) | magic pass |
| cable_1973_100133 | `cable_1973_100133.djvu` | JB2 bilevel | 2 | not checked | 15486 | U.S. federal government work, 17 U.S.C. 105 | [Internet Archive](https://archive.org/details/State-Dept-cable-1973-100133) | magic pass |
| conquete_paix | `conquete_paix.djvu` | mixed IW44 + JB2 | unknown | not checked | 1717050 | pre-1928 publication | [Internet Archive](https://archive.org/details/TriompheSagesseValeur) | magic pass |
| pathogenic_bacteria_1896 | `pathogenic_bacteria_1896.djvu` | large mixed document | 520 | not checked | 26562908 | published 1896 | [Internet Archive](https://archive.org/details/PathogenicBacteria) | magic pass |
| war_1812 | `war_1812.djvu` | newspaper / photo-heavy scan | 8 | not checked | 919707 | pre-1928 publication | [Internet Archive](https://archive.org/details/warv1n2wood) | magic pass |
| goody_twoshoes | `goody_twoshoes.djvu` | illustrated mixed layout | 16 | not checked | 1060487 | IA `NOT_IN_COPYRIGHT` | [Internet Archive](https://archive.org/details/goodytwoshoes00newyiala) | magic pass |
| map_atlas_sample | `map_atlas_sample.djvu` | map / line-art | 2 | not checked | 413070 | IA `NOT_IN_COPYRIGHT`; published 1910 | [Internet Archive](https://archive.org/details/graphicatlasofwo00bart) | magic pass |
| chinese_cookbook_sample | `chinese_cookbook_sample.djvu` | CJK text | 5 | not checked | 218742 | Library of Congress public-domain note; published 1917 | [Internet Archive](https://archive.org/details/chinesecookbook00chan) | magic pass |
| cyrillic_simonovich_co2 | `cyrillic_simonovich_co2.djvu` | Cyrillic text | 12 | not checked | 171563 | Public Domain Mark; published 1905 | [Internet Archive](https://archive.org/details/20200630_simonovich_uglekislota) | magic pass |
| big_scanned_page | `big_scanned_page.djvu` | photo / maskless IW44 | 1 | not checked | 584365 | Unlicense | [djvu.js test asset](https://github.com/galkahana/djvu.js/tree/master/tests/fixtures/big-scanned-page.djvu) | magic pass |
| navm_fgbz | `navm_fgbz.djvu` | NAVM / shared JB2 | 6 | not checked | 100415 | Unlicense | [djvu.js test asset](https://github.com/RussCoder/djvujs) | magic pass |
| links | `links.djvu` | NAVM outline | 1 | not checked | 440 | Unlicense | [djvu.js test asset](https://github.com/RussCoder/djvujs) | magic pass |
| carte_th44 | `carte_th44.djvu` | TH44, annotations, hyperlinks, text | 1 | not checked | 154282 | Unlicense | [djvu.js test asset](https://github.com/RussCoder/djvujs) | magic pass |

The additional three positive assets come from `tests/fixtures` in the same
pinned `djvu-rs` revision. That fixture directory attributes its copied
`djvu.js` assets to the Unlicense. Existing corpus files cover `TXTz`, shared
`Djbz`, and `INCL`; `navm_fgbz` and `links` add navigation/bookmarks, while
`carte_th44` adds `TH44`, annotations, hyperlinks, and text.

Four local negative entries are generated from the pinned positives:
`corrupt_truncated`, `corrupt_form_length`, `corrupt_chunk_length`, and
`corrupt_missing_incl`. Their manifest entries declare `expected_result: fail`
and respectively require `truncated_input`, `invalid_form_length`,
`invalid_chunk_length`, and `missing_incl`; no corrupted byte stream is
downloaded or stored in Git.

The precise rights note and any redistribution flag live in [`manifest.json`](manifest.json). They report upstream and source metadata rather than adding a new legal conclusion. `watchmaker` is the only fixture marked `redistribution_review_required: true`.

The original curated README describes `watchmaker` and `cable_1973_100133` as one-page files. A direct `DjVuDocument::get_pages_num()` check of the pinned bytes reports 12 and 2 pages respectively, so the local expected values use those observed counts.

## Layout

```text
corpus/
  files/          downloaded DjVu files (ignored)
    generated/    deterministic negative derivatives (ignored)
  manifests/      per-file diagnostic JSON
  dumps/          local djvudump output (ignored)
  manifest.json   canonical metadata and observed values
  SHA256SUMS.txt  generated checksums
  fetch-corpus.ps1
```

Do not add `files/` or `dumps/` to a user release archive. The corpus is development/test input only.
