# Cost of one azslc run as a function of input size.
#
# Two numbers come out: the average, which is what a whole shader costs divided by its lines, and the
# marginal, which is the slope of a least-squares fit and is the one that says what removing a line is
# worth. They differ because a run has a fixed cost that does not scale -- process spawn, ANTLR's static
# initialisation, the SRG and reflection work that depends on the shader's interface rather than its length.
#
# Only the marginal figure should be used to price a fidelity cut. Quoting the average overstates the saving
# by whatever share of the run is fixed.
#
# Uses the same command line the Asset Processor runs, against the .azslin products already in the cache,
# with the AP out of the picture. Round-robin across inputs with a discarded warm-up and medians, per
# z_PREVIEW_PERFORMANCE.md section 7.3.

param(
    [string]$Azslc = "C:\Users\medin\O3DE\Projects\fork_t\build\windows\bin\profile\azslc.exe",
    [string]$CacheRoot = "C:\Users\medin\O3DE\Projects\fork_t\Cache\pc",
    [int]$Rounds = 5,
    [int]$SampleCount = 9
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $Azslc)) { throw "azslc not found: $Azslc" }

$tmp = Join-Path $env:TEMP "azslc_per_line"
New-Item -ItemType Directory -Force $tmp | Out-Null
$common = @("--Zpr", "--W1", "--strip-unused-srgs", "--root-const=128", "--sc-options", "--namespace=dx")

function Invoke-Azslc {
    param([string[]] $Argv)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Azslc
    $psi.Arguments = (($Argv | ForEach-Object { '"' + $_ + '"' }) -join " ")
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.StandardOutput.ReadToEnd() | Out-Null
    $p.StandardError.ReadToEnd() | Out-Null
    $p.WaitForExit()
    $sw.Stop()
    return [pscustomobject]@{ Ms = $sw.Elapsed.TotalMilliseconds; ExitCode = $p.ExitCode }
}

# Content lines, not raw lines: the preprocessor leaves long runs of blanks where it expanded includes out,
# and counting those makes a shader look bigger than the work it represents.
function Get-ContentLineCount {
    param([string] $Path)
    $count = 0
    foreach ($line in [System.IO.File]::ReadLines($Path)) {
        if ($line.Length -eq 0) { continue }
        $trimmed = $line.TrimStart()
        if ($trimmed.Length -eq 0) { continue }
        if ($trimmed[0] -eq '#') { continue }   # line markers
        $count++
    }
    return $count
}

# Spread the sample across the size range rather than taking the first N, so the slope is fitted over
# something wider than run-to-run noise.
$all = @(Get-ChildItem $CacheRoot -Recurse -Filter "*_dx12.azslin" | Sort-Object Length)
if ($all.Count -eq 0) { throw "no .azslin products under $CacheRoot" }
$samples = @()
for ($i = 0; $i -lt $SampleCount; $i++) {
    $idx = [int](($all.Count - 1) * $i / [double]($SampleCount - 1))
    $samples += $all[$idx]
}
$samples = @($samples | Sort-Object FullName -Unique)

$rows = @()
foreach ($f in $samples) {
    $rows += [pscustomobject]@{ File = $f; Lines = (Get-ContentLineCount $f.FullName); Times = (New-Object System.Collections.ArrayList) }
}

Write-Host "azslc : $Azslc"
Write-Host ("inputs: {0}, {1} measured rounds plus a warm-up, round-robin`n" -f $rows.Count, $Rounds)

for ($r = 0; $r -le $Rounds; $r++) {
    foreach ($row in $rows) {
        $res = Invoke-Azslc -Argv (@($row.File.FullName) + $common + @("--full", "-o", (Join-Path $tmp "o.hlsl")))
        if ($res.ExitCode -ne 0) { Write-Host ("  ! {0} exited {1}" -f $row.File.Name, $res.ExitCode) -ForegroundColor Yellow; continue }
        if ($r -gt 0) { [void]$row.Times.Add($res.Ms) }
    }
}

function Median($a) { $x = @($a | Sort-Object); $x[[int](($x.Count - 1) / 2)] }

"{0,9} {1,9} {2,9}  {3}" -f "lines", "ms", "ms/line", "shader"
$fit = @()
foreach ($row in $rows) {
    if ($row.Times.Count -eq 0) { continue }
    $ms = Median $row.Times
    $fit += [pscustomobject]@{ X = [double]$row.Lines; Y = [double]$ms }
    "{0,9:N0} {1,9:N0} {2,9:N3}  {3}" -f $row.Lines, $ms, ($ms / $row.Lines), $row.File.Name
}

if ($fit.Count -ge 2) {
    $n = $fit.Count
    $sx = ($fit | Measure-Object X -Sum).Sum
    $sy = ($fit | Measure-Object Y -Sum).Sum
    $sxx = 0.0; $sxy = 0.0
    foreach ($pt in $fit) { $sxx += $pt.X * $pt.X; $sxy += $pt.X * $pt.Y }
    $slope = ($n * $sxy - $sx * $sy) / ($n * $sxx - $sx * $sx)
    $intercept = ($sy - $slope * $sx) / $n
    $avg = $sy / $sx
    Write-Host ""
    Write-Host ("average  : {0:N3} ms per line   (total time / total lines)" -f $avg)
    Write-Host ("marginal : {0:N3} ms per line   (least-squares slope; use this one to price a line cut)" -f $slope)
    Write-Host ("intercept: {0:N0} ms             (fit only -- NOT a physical floor: a trivial input still costs ~25 ms of spawn," -f $intercept)
    Write-Host  "                               so the fit is dominated by the large inputs and understates the fixed cost)"
}
