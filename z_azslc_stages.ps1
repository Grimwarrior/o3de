# Decompose one azslc run into parse / semantic+middle-end / HLSL emission / reflection dumps,
# using only azslc's own mode flags. No rebuild and no instrumentation required.
#
#   --syntax    stops after the parse                      (Main.cpp:544)
#   --semantic  adds the semantic walk, MiddleEnd, Validate (Main.cpp:697 gates emission off)
#   (neither)   adds emitter.Run                            (Main.cpp:793)
#   --full      adds the five reflection dumps              (Main.cpp:755-759)
#
# so consecutive differences attribute the total. Arms run round-robin with a discarded warm-up and
# medians reported, per z_PREVIEW_PERFORMANCE.md section 7.3 -- a harness without a baseline arm
# reports its own overhead as the result.

param(
    [string]$Azslc = "C:\Users\medin\O3DE\Projects\fork_t\build\windows\bin\profile\azslc.exe",
    [string]$InFile = "C:\Users\medin\O3DE\Projects\fork_t\Cache\pc\assets\materialcanvaspreview\assets\crystal_shader_materialcanvaspreview_transparent_standardlighting_dx12.azslin",
    [int]$Rounds = 7
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $Azslc)) { throw "azslc not found: $Azslc" }
if (-not (Test-Path $InFile)) { throw "input not found: $InFile" }

$tmp = Join-Path $env:TEMP "azslc_stages"
New-Item -ItemType Directory -Force $tmp | Out-Null

# Baseline arm: a trivial shader through the identical flag set, so process spawn and ANTLR's static
# init are attributed to the baseline rather than to the parse.
$triv = Join-Path $tmp "trivial.azsl"
Set-Content -Path $triv -Encoding utf8 -Value @'
ShaderResourceGroupSemantic slot1 { FrequencyId = 1; };
ShaderResourceGroup S : slot1 { float4 m_c; };
float4 MainPS() : SV_Target0 { return S.m_c; }
'@

# The AP's own command line, from the job log, minus the parts that select a stage.
$common = @("--Zpr","--W1","--strip-unused-srgs","--root-const=128","--sc-options","--namespace=dx")

# Input file goes FIRST: --namespace is an unbounded vector option and swallows a trailing
# positional (z_PREVIEW_PERFORMANCE.md section 7.10).
$arms = [ordered]@{
    "baseline (full, trivial)" = { param($o) @($triv) + $common + @("--full","-o",$o) }
    "1 syntax   (parse)"       = { param($o) @($InFile) + $common + @("--syntax") }
    "2 semantic (+sema/middle)"= { param($o) @($InFile) + $common + @("--semantic") }
    "3 hlsl     (+emit)"       = { param($o) @($InFile) + $common + @("-o",$o) }
    "4 full     (+reflection)" = { param($o) @($InFile) + $common + @("--full","-o",$o) }
}

function Invoke-Azslc {
    param([string[]] $Argv)

    # Start-Process -Wait carries about a second of PowerShell-side overhead on this machine and
    # reports it as the measurement -- the same artefact z_PREVIEW_PERFORMANCE.md section 7.3
    # records for an earlier harness. Drive Process directly, as z_dxc_optlevel.ps1 does.
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName               = $Azslc
    $psi.Arguments              = (($Argv | ForEach-Object { '"' + $_ + '"' }) -join " ")
    $psi.UseShellExecute        = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $psi.CreateNoWindow         = $true

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $proc = [System.Diagnostics.Process]::Start($psi)
    $stdout = $proc.StandardOutput.ReadToEnd()
    $stderr = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit()
    $sw.Stop()

    return [pscustomobject]@{ Ms = $sw.Elapsed.TotalMilliseconds; ExitCode = $proc.ExitCode; StdErr = $stderr }
}

$samples = @{}
foreach ($k in $arms.Keys) { $samples[$k] = New-Object System.Collections.ArrayList }

Write-Host "azslc  : $Azslc"
Write-Host "input  : $(Split-Path -Leaf $InFile)  ($((Get-Content $InFile | Measure-Object -Line).Lines) lines)"
Write-Host "rounds : $Rounds (plus one discarded warm-up), round-robin`n"

for ($r = 0; $r -le $Rounds; $r++) {
    foreach ($k in $arms.Keys) {
        $out = Join-Path $tmp "out.hlsl"
        $argv = & $arms[$k] $out
        $res = Invoke-Azslc -Argv $argv
        if ($res.ExitCode -ne 0) {
            Write-Host "  ! $k exited $($res.ExitCode)" -ForegroundColor Yellow
            ($res.StdErr -split "`n") | Select-Object -First 3 | ForEach-Object { Write-Host "    $_" }
        }
        if ($r -gt 0) { [void]$samples[$k].Add($res.Ms) }
    }
}

function Median($a) { $s = @($a | Sort-Object); $s[[int](($s.Count - 1) / 2)] }

Write-Host "`n=== medians (ms) ==="
"{0,-28} {1,8} {2,8} {3,8}" -f "arm","median","min","max"
$med = [ordered]@{}
foreach ($k in $arms.Keys) {
    $v = $samples[$k]
    $med[$k] = Median $v
    "{0,-28} {1,8:N0} {2,8:N0} {3,8:N0}" -f $k, $med[$k], ($v | Measure-Object -Minimum).Minimum, ($v | Measure-Object -Maximum).Maximum
}

$keys = @($arms.Keys)
$b = $med[$keys[0]]
Write-Host "`n=== attribution, spawn baseline ($([int]$b) ms) subtracted ==="
$prev = $b
$total = $med[$keys[4]] - $b
for ($i = 1; $i -lt 5; $i++) {
    $cur = $med[$keys[$i]]
    $d = $cur - $prev
    $label = switch ($i) { 1 {"parse"} 2 {"semantic + middle-end"} 3 {"HLSL emission"} 4 {"reflection dumps (ia/om/srg/options/bindingdep)"} }
    "{0,-46} {1,6:N0} ms  {2,5:N1}%" -f $label, $d, (100 * $d / $total)
    $prev = $cur
}
"{0,-46} {1,6:N0} ms" -f "total (azslc work, spawn excluded)", $total
