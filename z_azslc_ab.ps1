# Compare two azslc builds: first for identical output, then for speed.
#
# Correctness runs both binaries over every .azslin the Asset Processor has left in the project cache
# and compares all six products of each run -- the HLSL and the five reflection dumps. Any azslc change
# has to come out of this with zero differences before its timing means anything.
#
# Two things this harness learned the hard way, both of them ways to get a confident wrong answer:
#
#  * Both binaries must sit NEXT TO their DLLs. A copy in %TEMP% dies with 0xC0000135 (STATUS_DLL_NOT_
#    FOUND) before it parses anything, and two runs that both produce nothing compare equal -- 725 of
#    725 "identical", every file, in about a second. The exit-code and empty-output guards below exist
#    to make that failure loud instead of green.
#  * Timing must not go through Start-Process. It adds about a second of PowerShell-side overhead and
#    reports it as the result, which is the artefact z_PREVIEW_PERFORMANCE.md section 7.3 records.
#    Drive System.Diagnostics.Process directly, and keep a baseline arm (a trivial shader) so a harness
#    that has started measuring itself says so.
#
# Build the two binaries by checking out each revision, building the Azslc target, and copying the
# result into the build's bin\profile as azslc_baseline.exe and azslc_new.exe.

param(
    [string]$Old = "C:\Users\medin\O3DE\Projects\fork_t\build\windows\bin\profile\azslc_baseline.exe",
    [string]$New = "C:\Users\medin\O3DE\Projects\fork_t\build\windows\bin\profile\azslc_new.exe",
    [string]$CacheRoot = "C:\Users\medin\O3DE\Projects\fork_t\Cache\pc",
    [string]$TimedInput = "C:\Users\medin\O3DE\Projects\fork_t\Cache\pc\assets\materialcanvaspreview\assets\crystal_shader_materialcanvaspreview_transparent_standardlighting_dx12.azslin",
    [int]$Rounds = 9,
    [switch]$SkipVerify
)

$ErrorActionPreference = "Stop"
foreach ($e in @($Old, $New)) { if (-not (Test-Path $e)) { throw "azslc not found: $e" } }

$T = Join-Path $env:TEMP "azslc_ab"
Remove-Item $T -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$T\old" | Out-Null
New-Item -ItemType Directory -Force "$T\new" | Out-Null

# The AP's own azslc command line, from the job log, minus the input and the output.
$common = @("--Zpr","--W1","--strip-unused-srgs","--root-const=128","--sc-options")

function Invoke-Azslc {
    param([string]$Exe, [string[]]$Argv)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    # Input file first: --namespace is an unbounded vector option and swallows a trailing positional
    # (z_PREVIEW_PERFORMANCE.md section 7.10).
    $psi.Arguments = (($Argv | ForEach-Object { '"' + $_ + '"' }) -join " ")
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.StandardOutput.ReadToEnd() | Out-Null
    $p.StandardError.ReadToEnd()  | Out-Null
    $p.WaitForExit()
    $sw.Stop()
    return [pscustomobject]@{ Ms = $sw.Elapsed.TotalMilliseconds; ExitCode = $p.ExitCode }
}

if (-not $SkipVerify) {
    function RunInto($exe, $src, $dir, $ns) {
        Get-ChildItem $dir -File | Remove-Item -Force
        return (Invoke-Azslc -Exe $exe -Argv (@($src) + $common + @("--namespace=$ns","--full","-o",(Join-Path $dir "o.hlsl")))).ExitCode
    }

    $same = 0; $diff = 0; $broken = 0; $names = @()
    $files = @(Get-ChildItem $CacheRoot -Recurse -Filter "*.azslin")
    Write-Host ("verifying {0} azslin inputs, identical flags, old vs new`n" -f $files.Count)
    foreach ($f in $files) {
        $ns = if ($f.Name -match "_vulkan") { "vk" } else { "dx" }
        $eo = RunInto $Old $f.FullName "$T\old" $ns
        $en = RunInto $New $f.FullName "$T\new" $ns
        if ($eo -ne $en)  { Write-Host ("EXIT MISMATCH {0}: old={1} new={2}" -f $f.Name,$eo,$en); $broken++; continue }
        if ($eo -ne 0)    { Write-Host ("BOTH FAILED   {0}: exit={1}" -f $f.Name,$eo);            $broken++; continue }
        $o = @(Get-ChildItem "$T\old" -File | Sort-Object Name)
        $n = @(Get-ChildItem "$T\new" -File | Sort-Object Name)
        if ($o.Count -eq 0) { Write-Host ("NO OUTPUT     {0}" -f $f.Name); $broken++; continue }
        $bad = $o.Count -ne $n.Count
        if (-not $bad) {
            for ($i = 0; $i -lt $o.Count; $i++) {
                if ($o[$i].Name -ne $n[$i].Name -or (Get-FileHash $o[$i].FullName).Hash -ne (Get-FileHash $n[$i].FullName).Hash) { $bad = $true; break }
            }
        }
        if ($bad) { $diff++; $names += $f.Name } else { $same++ }
    }
    Write-Host ""
    Write-Host ("identical : {0}" -f $same)
    Write-Host ("differing : {0}" -f $diff)
    Write-Host ("unusable  : {0}   (exit mismatch, both-failed, or no output -- a zero here is what makes the row above mean anything)" -f $broken)
    if ($names.Count) { Write-Host "`nfirst differing:"; $names | Select-Object -First 15 }
    if ($diff -or $broken) { Write-Host "`nnot timing a build that does not reproduce the old output." -ForegroundColor Yellow; return }
    Write-Host ""
}

# Baseline arm: a trivial shader through the identical flags, so process spawn and static init are
# charged to the baseline instead of to the parse.
$triv = Join-Path $T "trivial.azsl"
Set-Content -Path $triv -Encoding utf8 -Value @'
ShaderResourceGroupSemantic slot1 { FrequencyId = 1; };
ShaderResourceGroup S : slot1 { float4 m_c; };
float4 MainPS() : SV_Target0 { return S.m_c; }
'@

$dx = $common + @("--namespace=dx")
$arms = [ordered]@{
    "baseline spawn (trivial)"   = @{ E = $New; A = @($triv)       + $dx + @("--full","-o","$T\t.hlsl") }
    "OLD  parse only (--syntax)" = @{ E = $Old; A = @($TimedInput) + $dx + @("--syntax") }
    "NEW  parse only (--syntax)" = @{ E = $New; A = @($TimedInput) + $dx + @("--syntax") }
    "OLD  full run (AP command)" = @{ E = $Old; A = @($TimedInput) + $dx + @("--full","-o","$T\o.hlsl") }
    "NEW  full run (AP command)" = @{ E = $New; A = @($TimedInput) + $dx + @("--full","-o","$T\n.hlsl") }
}

$s = @{}; foreach ($k in $arms.Keys) { $s[$k] = New-Object System.Collections.ArrayList }
for ($r = 0; $r -le $Rounds; $r++) {
    foreach ($k in $arms.Keys) {
        $res = Invoke-Azslc -Exe $arms[$k].E -Argv $arms[$k].A
        if ($res.ExitCode -ne 0) { Write-Host ("  ! {0} exited {1}" -f $k, $res.ExitCode) -ForegroundColor Yellow }
        if ($r -gt 0) { [void]$s[$k].Add($res.Ms) }
    }
}

function Median($a) { $x = @($a | Sort-Object); $x[[int](($x.Count - 1) / 2)] }

Write-Host ("{0} measured rounds, 1 warm-up discarded, round-robin, medians`n" -f $Rounds)
"{0,-28} {1,8} {2,8} {3,8}" -f "arm","median","min","max"
foreach ($k in $arms.Keys) {
    $v = $s[$k]
    "{0,-28} {1,8:N0} {2,8:N0} {3,8:N0}" -f $k, (Median $v), ($v|Measure-Object -Minimum).Minimum, ($v|Measure-Object -Maximum).Maximum
}

$b = Median $s["baseline spawn (trivial)"]
$po = (Median $s["OLD  parse only (--syntax)"]) - $b
$pn = (Median $s["NEW  parse only (--syntax)"]) - $b
$fo = (Median $s["OLD  full run (AP command)"]) - $b
$fn = (Median $s["NEW  full run (AP command)"]) - $b
Write-Host ""
Write-Host ("spawn baseline {0:N0} ms subtracted:" -f $b)
Write-Host ("  parse    old {0,5:N0} -> new {1,5:N0} ms   ({2:N0} ms, {3:N0}%)" -f $po,$pn,($po-$pn),(100*($po-$pn)/$po))
Write-Host ("  full run old {0,5:N0} -> new {1,5:N0} ms   ({2:N0} ms, {3:N0}%)" -f $fo,$fn,($fo-$fn),(100*($fo-$fn)/$fo))
