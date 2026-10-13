<#
.SYNOPSIS
    Splits DXC's time into "parse the whole file" and "optimise what the entry point reaches", to bound what a
    dead-code-elimination pass in AZSLc could possibly save.

.DESCRIPTION
    The argument for DCE is that AZSLc emits every symbol, so DXC parses the whole shader library again on every
    compile. True. The question nobody has answered is what that parse actually costs, because DXC does its own
    dead-code elimination immediately after parsing -- so pruning earlier removes the parse of the discarded text
    and nothing else.

    This measures it directly, with no compiler work:

      REAL   the file compiled with its real entry point.
      NULL   the same file, same bytes, compiled with a trivial entry point appended. DXC still parses everything,
             then discards nearly all of it. What remains is parse plus a trivial codegen.

    NULL is therefore an upper bound on what DCE could recover: prune half the text and you save at most half of
    NULL. REAL minus NULL is optimisation of reachable code, which DCE cannot touch at all.

    Note the asymmetry this is testing. In an Asset Processor log for a MainPipeline shader, the same file compiled
    174 ms as a vertex entry point and 2,191 ms as a pixel one. The parse is identical across those two, so most of
    that 2,191 ms is optimisation. This checks whether the preview shader behaves the same way.

.EXAMPLE
    .\z_dxc_split.ps1 -Runs 3
#>

param(
    [string] $Project = "C:\Users\medin\O3DE\Projects\fork_t",
    [string] $Hlsl    = "",
    [string] $Dxc     = "",
    [string] $Entry   = "PixelShader",
    [string] $Profile = "ps_6_2",
    [int]    $Runs    = 3
)

$ErrorActionPreference = "Continue"

if (-not $Dxc) {
    $Dxc = Join-Path $Project "build\windows\bin\profile\Builders\DirectXShaderCompiler\dxc.exe"
}
if (-not (Test-Path $Dxc)) { Write-Host "dxc.exe not found at $Dxc" -ForegroundColor Red; return }

# The .hlsl.prepend is what the Asset Processor actually hands DXC: the platform header plus AZSLc's output. Using
# the bare .hlsl instead would measure a file that never gets compiled.
if (-not $Hlsl) {
    $candidates = Get-ChildItem -Path (Join-Path $Project "user\AssetProcessorTemp") -Recurse -Filter "*materialcanvaspreview*.hlsl.prepend" -ErrorAction SilentlyContinue |
        Sort-Object Length -Descending
    if (-not $candidates) {
        Write-Host "No preview .hlsl.prepend under user\AssetProcessorTemp. Compile a graph, or pass -Hlsl." -ForegroundColor Red
        return
    }
    # Not every shader defines every entry point: a depth pass has VertexShader and no PixelShader, so picking the
    # most recent file blind fails with "missing entry point definition". Require the entry to be present, and
    # prefer the largest such file, since the whole point is to measure parse cost on a big one.
    $withEntry = $candidates | Where-Object { Select-String -Path $_.FullName -Pattern "\b$Entry\s*\(" -Quiet }
    if (-not $withEntry) {
        Write-Host "No preview shader defines an entry point named '$Entry'. Entry points found:" -ForegroundColor Red
        foreach ($c in ($candidates | Select-Object -First 4)) {
            $found = (Select-String -Path $c.FullName -Pattern "^\S+\s+(VertexShader|PixelShader|ComputeShader)\s*\(" -AllMatches |
                      ForEach-Object { $_.Matches.Groups[1].Value } | Sort-Object -Unique) -join ", "
            Write-Host ("   {0,-62} {1}" -f $c.Name, $found) -ForegroundColor DarkGray
        }
        Write-Host "Re-run with -Entry <name> (and a matching -Profile, e.g. vs_6_2)." -ForegroundColor Red
        return
    }
    $Hlsl = $withEntry[0].FullName
}
Write-Host "input: $(Split-Path $Hlsl -Leaf)" -ForegroundColor DarkGray
Write-Host "       $((Get-Item $Hlsl).Length) bytes, $((Get-Content $Hlsl).Count) lines" -ForegroundColor DarkGray

$temp = Join-Path $env:TEMP "dxc_split"
New-Item -ItemType Directory -Force -Path $temp | Out-Null

# The null variant is the real file with a do-nothing entry point appended. Same bytes plus four lines, so the
# parse is the same work; only what is reachable changes.
$nullEntry = "NullEntryForTiming"
$nullFile  = Join-Path $temp "null_entry.hlsl"
Copy-Item $Hlsl $nullFile -Force

# The null entry has to be legal for the profile being measured: SV_Target is a pixel-stage semantic and a vertex
# profile rejects it outright ("invalid semantic 'SV_Target' for vs 6.2"), which looks like a measurement failure
# rather than a malformed probe.
switch -Wildcard ($Profile) {
    "vs_*" { $nullBody = "float4 $nullEntry() : SV_POSITION { return float4(0,0,0,0); }" }
    "cs_*" { $nullBody = "[numthreads(1,1,1)] void $nullEntry() { }" }
    default { $nullBody = "float4 $nullEntry() : SV_Target0 { return float4(0,0,0,0); }" }
}
Add-Content -Path $nullFile -Value @"

// Appended by z_dxc_split.ps1. Reaches nothing, so DXC parses the whole file and then discards essentially all of
// it -- which is exactly the cost a DCE pass in AZSLc would be removing.
$nullBody
"@

$script:lastSpread = ""
function Measure-Dxc([string] $file, [string] $entry, [string] $prof) {
    $times = @()
    for ($i = 1; $i -le $Runs; $i++) {
        $out = Join-Path $temp "out_$entry`_$i.bin"
        $err = Join-Path $temp "err_$entry`_$i.txt"
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $p = Start-Process -FilePath $Dxc -NoNewWindow -Wait -PassThru -RedirectStandardError $err `
             -ArgumentList @("-E", $entry, "-T", $prof, "-Zpr", "-enable-16bit-types", "-Fo", $out, $file)
        $sw.Stop()
        if ($p.ExitCode -ne 0) {
            Write-Host "  $entry run ${i}: dxc failed (exit $($p.ExitCode))" -ForegroundColor Yellow
            if ((Get-Content $err -Raw -ErrorAction SilentlyContinue) -match "Opcode Sample not valid in shader model vs") {
                Write-Host "    This .hlsl.prepend predates the CS_SAMPLERS fix in the vertex evaluation templates." -ForegroundColor Yellow
                Write-Host "    Re-save the graph to regenerate it, or measure the pixel entry point instead." -ForegroundColor Yellow
            }
            Get-Content $err -ErrorAction SilentlyContinue | Select-Object -First 3 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
            return $null
        }
        $times += $sw.Elapsed.TotalMilliseconds
    }
    $script:lastSpread = "{0:N0}-{1:N0}" -f ($times | Measure-Object -Minimum).Minimum, ($times | Measure-Object -Maximum).Maximum
    ($times | Sort-Object)[[int]([math]::Floor($times.Count / 2))]
}

# dxc.exe process startup, DLL load and any on-access virus scanning are paid on every invocation and have nothing
# to do with the shader. Without measuring them, a large fixed cost makes every compile look the same and the
# REAL/NULL difference vanishes into it -- which is exactly what a first run of this script showed.
function Measure-Overhead() {
    $tiny = Join-Path $temp "tiny.hlsl"
    Set-Content -Path $tiny -Value "float4 TinyPS() : SV_Target0 { return float4(1,0,0,1); }"
    Measure-Dxc $tiny "TinyPS" "ps_6_2"
}

Write-Host ""
$overhead = Measure-Overhead
$ovSpread = $script:lastSpread
$real  = Measure-Dxc $Hlsl     $Entry     $Profile
$reSpread = $script:lastSpread
$null_ = Measure-Dxc $nullFile $nullEntry $Profile
$nuSpread = $script:lastSpread

if ($null -eq $real -or $null -eq $null_) { Write-Host "Could not complete both measurements." -ForegroundColor Red; return }

$realNet = $real  - $overhead
$nullNet = $null_ - $overhead
$opt     = $realNet - $nullNet

Write-Host ("  {0,-30} {1,8:N0} ms   spread {2}" -f "dxc fixed overhead", $overhead, $ovSpread) -ForegroundColor DarkGray
Write-Host ("  {0,-30} {1,8:N0} ms   spread {2}" -f "REAL  ($Entry)", $real, $reSpread)
Write-Host ("  {0,-30} {1,8:N0} ms   spread {2}" -f "NULL  (nothing reachable)", $null_, $nuSpread)
Write-Host ""
Write-Host ("  {0,-30} {1,8:N0} ms   parse + sema of the whole file" -f "NULL - overhead", $nullNet)
Write-Host ("  {0,-30} {1,8:N0} ms   optimisation, unreachable to DCE" -f "REAL - NULL", $opt)
Write-Host ""

if ($opt -lt 0 -or [math]::Abs($opt) -lt 30) {
    Write-Host "REAL and NULL are within noise of each other. Either this shader optimises to almost nothing once" -ForegroundColor Yellow
    Write-Host "compiled, or the difference is below what three runs can resolve. Re-run with more -Runs before" -ForegroundColor Yellow
    Write-Host "concluding anything; a negative value here is not a real measurement." -ForegroundColor Yellow
} else {
    Write-Host ("Of the work that is not fixed overhead, {0:N0}% is front end and {1:N0}% is optimisation." -f `
        (100.0 * $nullNet / [math]::Max($realNet,1)), (100.0 * $opt / [math]::Max($realNet,1)))
    Write-Host ("A DCE pass removing X% of the emitted text saves at most X% of {0:N0} ms, and none of the {1:N0} ms." -f $nullNet, $opt)
}
Write-Host ""
