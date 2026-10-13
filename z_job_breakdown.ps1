<#
.SYNOPSIS
    Decomposes a shader job into real compiler work, process-spawn overhead, and Asset Processor overhead.

.DESCRIPTION
    A shader job spawns five processes: azslc once, then dxc and dxsc once per shader stage. The job log reports
    wall time for each spawn (ExecuteShaderCompiler elapsedTimeMillis), and an instrumented azslc reports its own
    internal TOTAL. The difference between those two is what launching a process costs, and nothing in the phase
    tables we have been optimising can see it.

    That difference is worth knowing before optimising the compilers further. On one preview job:

        azslc  764 ms spawn   vs ~377 ms of measured internal work
        dxc     69 ms spawn   vs ~10 ms of measured compile work

    If those hold, most of a preview shader job is not compilation at all.

    To separate spawn cost from compile cost this also times each executable doing nothing (--version), which is
    a direct measurement of the floor rather than an inference from the difference.

    Run it right after a graph edit so the newest job log is the one you care about.

.EXAMPLE
    .\z_job_breakdown.ps1 -Filter "*materialcanvaspreview*" -Runs 5
#>

param(
    [string] $Project = "C:\Users\medin\O3DE\Projects\fork_t",
    [string] $Filter  = "*materialcanvaspreview*",
    [int]    $Runs    = 5,
    [int]    $Minutes = 120
)

$ErrorActionPreference = "Continue"
$builders = Join-Path $Project "build\windows\bin\profile\Builders"

# ---------------------------------------------------------------- the spawn floor, measured not inferred
Write-Host ""
Write-Host "=== cost to launch each compiler and have it do nothing ===" -ForegroundColor Cyan
$floors = @{}
foreach ($exe in @(
    @{ n = "azslc"; p = Join-Path $builders "AZSLc\azslc.exe";                  a = @("--version") },
    @{ n = "dxc";   p = Join-Path $builders "DirectXShaderCompiler\dxc.exe";    a = @("--version") },
    @{ n = "dxsc";  p = Join-Path $builders "DirectXShaderCompiler\dxsc.exe";   a = @("--help")    }
)) {
    if (-not (Test-Path $exe.p)) { Write-Host ("  {0,-8} not found" -f $exe.n) -ForegroundColor DarkGray; continue }
    $t = @()
    for ($i = 0; $i -lt $Runs; $i++) {
        $o = Join-Path $env:TEMP "floor_$($exe.n).txt"
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        Start-Process -FilePath $exe.p -ArgumentList $exe.a -NoNewWindow -Wait `
                      -RedirectStandardOutput $o -RedirectStandardError "$o.err" -ErrorAction SilentlyContinue | Out-Null
        $sw.Stop(); $t += $sw.Elapsed.TotalMilliseconds
    }
    $med = ($t | Sort-Object)[[int]([math]::Floor($t.Count / 2))]
    $floors[$exe.n] = $med
    Write-Host ("  {0,-8} {1,7:N0} ms   spread {2:N0}-{3:N0}   ({4} bytes)" -f `
        $exe.n, $med, ($t | Measure-Object -Minimum).Minimum, ($t | Measure-Object -Maximum).Maximum, (Get-Item $exe.p).Length)
}
Write-Host "  NOTE: measured through Start-Process, which adds its own overhead. Treat as an upper bound on the" -ForegroundColor DarkGray
Write-Host "        floor; the job log's own numbers are what the Asset Processor really pays." -ForegroundColor DarkGray

# ---------------------------------------------------------------- the job itself
$logs = Get-ChildItem -Path (Join-Path $Project "user\log\JobLogs") -Recurse -File -Filter "*.log" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like $Filter -and $_.Name -notlike "*createJobs*" -and $_.LastWriteTime -gt (Get-Date).AddMinutes(-$Minutes) } |
    Sort-Object LastWriteTime -Descending

if (-not $logs) { Write-Host "`nNo job log matching '$Filter' in the last $Minutes minutes." -ForegroundColor Yellow; return }

foreach ($log in ($logs | Select-Object -First 4)) {
    $lines = Get-Content $log.FullName -ErrorAction SilentlyContinue
    if (-not $lines) { continue }

    # Pair each "Executing '<exe>'" with the elapsed time that follows it.
    $spawns = @(); $pending = $null
    $azslcTotal = $null; $jobSeconds = $null
    foreach ($l in $lines) {
        if ($l -match "Builders[\\/](\w+)[\\/](\w+)\.exe") { $pending = $Matches[2] }
        elseif ($l -match "ExecuteShaderCompiler elapsedTimeMillis:\s*(\d+)" -and $pending) {
            $spawns += [pscustomobject]@{ Exe = $pending; Ms = [int] $Matches[1] }; $pending = $null
        }
        elseif ($l -match "^\s*TOTAL\s+([\d\.]+) ms") { $azslcTotal = [double] $Matches[1] }
        elseif ($l -match "Finished processing .* in ([\d\.]+) seconds") { $jobSeconds = [double] $Matches[1] }
    }
    if ($spawns.Count -eq 0) { continue }

    Write-Host ""
    Write-Host "=================================================================" -ForegroundColor Cyan
    Write-Host ($log.Name -replace "\.shader-.*$", "") -ForegroundColor Cyan
    Write-Host "=================================================================" -ForegroundColor Cyan

    $spawnSum = ($spawns | Measure-Object Ms -Sum).Sum
    $jobMs = if ($jobSeconds) { $jobSeconds * 1000 } else { $null }

    foreach ($s in $spawns) {
        $floor = if ($floors.ContainsKey($s.Exe)) { $floors[$s.Exe] } else { $null }
        $note = ""
        if ($s.Exe -eq "azslc" -and $azslcTotal) {
            $note = "internal {0:N0} ms, so {1:N0} ms outside the compiler" -f $azslcTotal, ($s.Ms - $azslcTotal)
        }
        Write-Host ("  {0,-8} {1,7:N0} ms   {2}" -f $s.Exe, $s.Ms, $note)
    }
    Write-Host ("  {0,-8} {1,7:N0} ms   five spawns" -f "SPAWNS", $spawnSum)
    if ($jobMs) {
        Write-Host ("  {0,-8} {1,7:N0} ms   whole job" -f "JOB", $jobMs)
        Write-Host ("  {0,-8} {1,7:N0} ms   Asset Processor around the compilers" -f "AP", ($jobMs - $spawnSum))
    }

    if ($azslcTotal) {
        $realWork = $azslcTotal
        Write-Host ""
        Write-Host ("  Measured compiler work is {0:N0} ms of a {1:N0} ms job ({2:N0}%)." -f `
            $realWork, $jobMs, (100.0 * $realWork / [math]::Max($jobMs,1)))
        Write-Host "  The rest is process launches and Asset Processor dispatch, which no amount of compiler tuning reaches."
    } else {
        Write-Host ""
        Write-Host "  No AZSLc phase table in this log, so its internal time is unknown and the spawn overhead cannot" -ForegroundColor Yellow
        Write-Host "  be separated. Re-instrument AZSLc (z_azslc_instrument.py) and edit a graph to get it." -ForegroundColor Yellow
    }
}
Write-Host ""
