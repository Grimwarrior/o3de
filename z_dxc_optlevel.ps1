# Measures what dxc actually spends on the Material Canvas preview pixel shader, and what two
# cheap changes to its command line would save.
#
# Arms:
#   O1+Fh   what the Asset Processor runs today
#   O1      same, minus -Fh
#   O0+Fh   -O0, keeping -Fh
#   O0      both
#   VS O1+Fh   the vertex entry point, for scale
#   baseline   a one-line shader, to separate process spawn from compile
#
# -Fh writes a full text disassembly of the object code. ShaderPlatformInterface.cpp:250 says what
# it is for: "used for counting dynamic branches" -- a diagnostic, read back at line 379 and
# reported as a byproduct. Nothing in the built shader depends on it.
#
# -O0 is the documented next step in the preview shader templates, with the warning attached: DXC's
# -O0 can defeat resource index resolution and emit DXIL that fails validation. The script checks
# for that rather than assuming, and reports the object code size per arm so a silently different
# result is visible.
#
# Arms are run round-robin with a discarded warm-up round, and the median is reported, because a
# previous harness in this project measured 1,017 ms for a one-line shader while the Asset Processor
# saw 59-175 ms for real ones. Whatever that fixed cost is, it lands on every arm equally, so the
# differences between arms survive it and the absolute numbers do not.

param(
    [int] $Reps = 7,
    [string] $Shader = "crystal_shader_materialcanvaspreview_transparent_standardlighting"
)

$ErrorActionPreference = "Stop"

$projectRoot = "C:\Users\medin\O3DE\Projects\fork_t"
$binRoot     = Join-Path $projectRoot "build\windows\bin\profile"
$dxc         = Join-Path $binRoot "Builders\DirectXShaderCompiler\dxc.exe"
$palHeader   = Join-Path $binRoot "Builders\ShaderHeaders\Platform\Windows\DX12\PlatformHeader.hlsli"
$hlsl        = Join-Path $projectRoot ("Cache\pc\assets\materialcanvaspreview\assets\" + $Shader + "_dx12.hlsl")

foreach ($p in @($dxc, $palHeader, $hlsl)) {
    if (-not (Test-Path $p)) { Write-Error "Not found: $p"; exit 1 }
}

$work = Join-Path $env:TEMP "z_dxc_optlevel"
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Path $work | Out-Null

# Reproduce what RHI::PrependFile does for DX12: the platform header, then the azslc output.
$prepend = Join-Path $work "input.hlsl.prepend"
$headerText = [System.IO.File]::ReadAllText($palHeader)
$hlslText   = [System.IO.File]::ReadAllText($hlsl)
[System.IO.File]::WriteAllText($prepend, $headerText + "`r`n" + $hlslText)

# A one-line shader, to price the process spawn on its own.
$trivial = Join-Path $work "trivial.hlsl"
[System.IO.File]::WriteAllText($trivial, "float4 PixelShader() : SV_Target0 { return 0; }`r`n")

$hlslLines = ([System.IO.File]::ReadAllLines($hlsl)).Count
Write-Host ""
Write-Host ("input : {0}" -f (Split-Path $hlsl -Leaf))
Write-Host ("        {0:N0} lines, {1:N0} bytes  (+ {2} byte platform header)" -f $hlslLines, $hlslText.Length, $headerText.Length)
Write-Host ("reps  : {0} measured, 1 warm-up, round-robin" -f $Reps)
Write-Host ""

function Invoke-Dxc {
    param([string[]] $DxcArgs)

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName               = $dxc
    $psi.Arguments              = ($DxcArgs -join " ")
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

    return [pscustomobject]@{
        Ms       = [int] $sw.Elapsed.TotalMilliseconds
        ExitCode = $proc.ExitCode
        StdErr   = $stderr
        StdOut   = $stdout
    }
}

# name, entry point, profile, optimization flag, emit -Fh, source file
$arms = @(
    @{ Name = "O1 +Fh  (AP today)"; Entry = "PixelShader";  Profile = "ps_6_2"; Opt = "-O1"; Fh = $true;  Src = $prepend },
    @{ Name = "O1      (no -Fh)  "; Entry = "PixelShader";  Profile = "ps_6_2"; Opt = "-O1"; Fh = $false; Src = $prepend },
    @{ Name = "O0 +Fh            "; Entry = "PixelShader";  Profile = "ps_6_2"; Opt = "-O0"; Fh = $true;  Src = $prepend },
    @{ Name = "O0      (no -Fh)  "; Entry = "PixelShader";  Profile = "ps_6_2"; Opt = "-O0"; Fh = $false; Src = $prepend },
    @{ Name = "VS O1 +Fh         "; Entry = "VertexShader"; Profile = "vs_6_2"; Opt = "-O1"; Fh = $true;  Src = $prepend },
    @{ Name = "baseline (1 line) "; Entry = "PixelShader";  Profile = "ps_6_2"; Opt = "-O1"; Fh = $true;  Src = $trivial }
)

foreach ($arm in $arms) {
    $arm.Times   = New-Object System.Collections.ArrayList
    $arm.Failed  = $false
    $arm.Error   = ""
    $arm.BinSize = 0
}

for ($round = 0; $round -le $Reps; $round++) {
    $warmup = ($round -eq 0)
    foreach ($arm in $arms) {
        $tag = ($arm.Name -replace '[^A-Za-z0-9]', '')
        $bin = Join-Path $work ($tag + ".dxil.bin")
        $txt = Join-Path $work ($tag + ".dxil.txt")

        $dxcArgs = @(
            "-E", $arm.Entry,
            "-T", $arm.Profile,
            "-Zpr",
            "-enable-16bit-types",
            $arm.Opt,
            "-Fo", ('"' + $bin + '"')
        )
        if ($arm.Fh) { $dxcArgs += @("-Fh", ('"' + $txt + '"')) }
        $dxcArgs += ('"' + $arm.Src + '"')

        $r = Invoke-Dxc -DxcArgs $dxcArgs

        if ($r.ExitCode -ne 0) {
            $arm.Failed = $true
            if ($arm.Error -eq "") {
                $firstLine = ($r.StdErr -split "`r?`n" | Where-Object { $_.Trim() -ne "" } | Select-Object -First 1)
                $arm.Error = "exit $($r.ExitCode): $firstLine"
            }
        } elseif (Test-Path $bin) {
            $arm.BinSize = (Get-Item $bin).Length
        }

        if (-not $warmup) { [void] $arm.Times.Add($r.Ms) }
    }
    if ($warmup) { Write-Host "warm-up done" } else { Write-Host -NoNewline "." }
}
Write-Host ""
Write-Host ""

function Get-Median {
    param([int[]] $Values)
    $s = $Values | Sort-Object
    $n = $s.Count
    if ($n -eq 0) { return 0 }
    if ($n % 2 -eq 1) { return $s[[int](($n - 1) / 2)] }
    return [int](($s[$n / 2 - 1] + $s[$n / 2]) / 2)
}

$fmt = "{0,-20} {1,8} {2,8} {3,8} {4,12} {5}"
Write-Host ($fmt -f "arm", "median", "min", "max", "dxil bytes", "")
Write-Host ("-" * 78)

$results = @{}
foreach ($arm in $arms) {
    $times  = [int[]] $arm.Times.ToArray()
    $median = Get-Median -Values $times
    $results[$arm.Name.Trim()] = $median

    $note = ""
    if ($arm.Failed) { $note = "FAILED  " + $arm.Error }

    Write-Host ($fmt -f $arm.Name.Trim(), $median, ($times | Measure-Object -Minimum).Minimum,
        ($times | Measure-Object -Maximum).Maximum, $arm.BinSize, $note)
}

Write-Host ""
$baseline = $results["baseline (1 line)"]
Write-Host ("Process spawn and fixed cost, from the one-line shader: {0} ms." -f $baseline)
Write-Host ("Everything below subtracts it, so these are compile times, not wall clock.")
Write-Host ""

$o1fh = $results["O1 +Fh  (AP today)"] - $baseline
$o1   = $results["O1      (no -Fh)"]   - $baseline
$o0fh = $results["O0 +Fh"]             - $baseline
$o0   = $results["O0      (no -Fh)"]   - $baseline
$vs   = $results["VS O1 +Fh"]          - $baseline

Write-Host ("  PS  -O1 +Fh   {0,5} ms   <- what the preview pays now" -f $o1fh)
Write-Host ("  PS  -O1       {0,5} ms   dropping -Fh alone saves {1} ms" -f $o1, ($o1fh - $o1))
Write-Host ("  PS  -O0 +Fh   {0,5} ms   dropping -O1 alone saves {1} ms" -f $o0fh, ($o1fh - $o0fh))
Write-Host ("  PS  -O0       {0,5} ms   both together save {1} ms" -f $o0, ($o1fh - $o0))
Write-Host ("  VS  -O1 +Fh   {0,5} ms" -f $vs)
Write-Host ""
Write-Host ("Files left in {0} for inspection." -f $work)
Write-Host ""
