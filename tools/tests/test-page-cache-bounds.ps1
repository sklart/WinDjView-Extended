[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$source = Get-Content -LiteralPath (Join-Path $root 'src\DjVuView.cpp') -Raw -Encoding Default
$header = Get-Content -LiteralPath (Join-Path $root 'src\DjVuView.h') -Raw -Encoding Default

function Require-Pattern([string]$pattern, [string]$description) {
    if ($source -notmatch $pattern) { throw "Missing $description" }
}

if ($header -notmatch 'set<int> m_observedPages') { throw 'Missing local observed-page tracking' }
Require-Pattern 'for \(int nDiff = 10; nDiff >= 0; --nDiff\)' 'bounded single/facing loop'
Require-Pattern 'while \(nCacheTop > 0.*rcDisplay\.bottom > nCacheTopLimit\)' 'bounded continuous backward expansion'
Require-Pattern 'while \(nCacheBottom \+ 1 < m_nPageCount.*rcDisplay\.top < nCacheBottomLimit\)' 'bounded continuous forward expansion'
Require-Pattern 'add\.reserve\(32\)' 'bounded update allocations'

function Get-SingleWindow([int]$count, [int]$page) {
    $pages = [Collections.Generic.HashSet[int]]::new()
    [void]$pages.Add(0); [void]$pages.Add($count - 1)
    for ($i = [Math]::Max(0, $page - 10); $i -le [Math]::Min($count - 1, $page + 10); ++$i) { [void]$pages.Add($i) }
    return ,$pages
}

function Get-ContinuousWindow([int]$count, [int]$topPage, [int]$bottomPage, [int]$height, [int]$viewport) {
    $top = $topPage; $bottom = $bottomPage
    $low = $height * $topPage - 10 * $viewport
    $high = $height * $topPage + 11 * $viewport
    while ($top -gt 0 -and $height * $top -gt $low) { --$top }
    while ($bottom + 1 -lt $count -and $height * ($bottom + 1) -lt $high) { ++$bottom }
    $pages = [Collections.Generic.HashSet[int]]::new()
    [void]$pages.Add(0); [void]$pages.Add($count - 1)
    for ($i = $top; $i -le $bottom; ++$i) { [void]$pages.Add($i) }
    return ,$pages
}

# One/two-page and large synthetic documents; all four layouts, resize, fast
# scrolling, and distant jumps. The selected page set must contain every page
# in the legacy render/decode window while remaining bounded for large docs.
foreach ($count in 1, 2, 500, 4096) {
    foreach ($page in 0, [int]($count / 2), ($count - 1)) {
        $single = Get-SingleWindow $count $page
        if ($single.Count -gt [Math]::Min($count, 23)) { throw "Single/Facing window is unbounded ($count pages)" }
        foreach ($i in 0..($count - 1)) {
            $legacy = ([Math]::Abs($i - $page) -le 10 -or $i -eq 0 -or $i -eq ($count - 1))
            if ($legacy -and -not $single.Contains($i)) { throw "Single/Facing selection diverged at page $i" }
        }

        foreach ($viewport in 600, 1200) {
            $continuous = Get-ContinuousWindow $count $page $page 1000 $viewport
            if ($count -ge 500 -and $continuous.Count -gt 30) { throw "Continuous window is unbounded ($count pages)" }
            foreach ($i in 0..($count - 1)) {
                $legacy = ($i * 1000 -lt $page * 1000 + 11 * $viewport -and ($i + 1) * 1000 -gt $page * 1000 - 10 * $viewport) -or $i -eq 0 -or $i -eq ($count - 1)
                if ($legacy -and -not $continuous.Contains($i)) { throw "Continuous selection diverged at page $i" }
            }
        }
    }
}

$count = 4096; $updates = 100
$timer = [Diagnostics.Stopwatch]::StartNew(); $entries = 0; $jobs = 0
for ($step = 0; $step -lt $updates; ++$step) {
    $page = ($step * 37) % $count
    $window = Get-ContinuousWindow $count $page $page 1000 900
    $entries += $window.Count; $jobs += $window.Count
}
$timer.Stop()
Write-Host ("PAGE_CACHE_BENCHMARK updates={0} elapsed_ms={1:F3} processed_entries={2} jobs={3} entries_per_update={4:F2}" -f $updates, $timer.Elapsed.TotalMilliseconds, $entries, $jobs, ($entries / $updates))
Write-Host 'Page cache bounded-selection regression: PASS'
