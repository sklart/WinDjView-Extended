# Real-world DjVu regression corpus

This directory defines the reproducible input corpus for the future WinDjView Extended real-DjVu compatibility and regression runner.  It is not a release asset and does not add any test runner, rendering comparison, OCR comparison, benchmark, or CI download.

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

`-Force` re-downloads every fixture. Downloads use a `.part` file, validate the `AT&TFORM` magic before promotion, then calculate SHA-256. A bad fixture, checksum mismatch, or known page-count mismatch exits non-zero, while independent fixtures continue to be processed. `SHA256SUMS.txt` is generated in manifest order and per-file manifests are regenerated from the top-level `manifest.json`.

The files under `files/` and raw data under `dumps/` are ignored by Git. The manifest, SHA list, script, README, and one diagnostic manifest per fixture are committed. In particular, the 520-page *Pathogenic Bacteria* fixture is always downloaded locally and is not in the release package.

## Fixtures and provenance

Actual sizes and SHA-256 values below are from the pinned source. `actual pages` is deliberately `not checked` until `djvudump` is available; expected counts are not presented as observed data.

| ID | Filename | Class | Expected pages | Actual pages | Size (bytes) | License / rights basis | Original source | Validation |
| --- | --- | --- | ---: | --- | ---: | --- | --- | --- |
| watchmaker | `watchmaker.djvu` | color IW44 | 1 | not checked | 183352 | IA public-domain designation; redistribution review required | [Internet Archive](https://archive.org/details/Watchmaker2001) | magic pass |
| cable_1973_100133 | `cable_1973_100133.djvu` | JB2 bilevel | 1 | not checked | 15486 | U.S. federal government work, 17 U.S.C. 105 | [Internet Archive](https://archive.org/details/State-Dept-cable-1973-100133) | magic pass |
| conquete_paix | `conquete_paix.djvu` | mixed IW44 + JB2 | unknown | not checked | 1717050 | pre-1928 publication | [Internet Archive](https://archive.org/details/TriompheSagesseValeur) | magic pass |
| pathogenic_bacteria_1896 | `pathogenic_bacteria_1896.djvu` | large mixed document | 520 | not checked | 26562908 | published 1896 | [Internet Archive](https://archive.org/details/PathogenicBacteria) | magic pass |
| war_1812 | `war_1812.djvu` | newspaper / photo-heavy scan | 8 | not checked | 919707 | pre-1928 publication | [Internet Archive](https://archive.org/details/warv1n2wood) | magic pass |
| goody_twoshoes | `goody_twoshoes.djvu` | illustrated mixed layout | 16 | not checked | 1060487 | IA `NOT_IN_COPYRIGHT` | [Internet Archive](https://archive.org/details/goodytwoshoes00newyiala) | magic pass |
| map_atlas_sample | `map_atlas_sample.djvu` | map / line-art | 2 | not checked | 413070 | IA `NOT_IN_COPYRIGHT`; published 1910 | [Internet Archive](https://archive.org/details/graphicatlasofwo00bart) | magic pass |
| chinese_cookbook_sample | `chinese_cookbook_sample.djvu` | CJK text | 5 | not checked | 218742 | Library of Congress public-domain note; published 1917 | [Internet Archive](https://archive.org/details/chinesecookbook00chan) | magic pass |
| cyrillic_simonovich_co2 | `cyrillic_simonovich_co2.djvu` | Cyrillic text | 12 | not checked | 171563 | Public Domain Mark; published 1905 | [Internet Archive](https://archive.org/details/20200630_simonovich_uglekislota) | magic pass |
| big_scanned_page | `big_scanned_page.djvu` | photo / maskless IW44 | 1 | not checked | 584365 | Unlicense | [djvu.js test asset](https://github.com/galkahana/djvu.js/tree/master/tests/fixtures/big-scanned-page.djvu) | magic pass |

The precise rights note and any redistribution flag live in [`manifest.json`](manifest.json). They report upstream and source metadata rather than adding a new legal conclusion. `watchmaker` is the only fixture marked `redistribution_review_required: true`.

## Layout

```text
corpus/
  files/          downloaded DjVu files (ignored)
  manifests/      per-file diagnostic JSON
  dumps/          local djvudump output (ignored)
  manifest.json   canonical metadata and observed values
  SHA256SUMS.txt  generated checksums
  fetch-corpus.ps1
```

Do not add `files/` or `dumps/` to a user release archive. The corpus is development/test input only.
