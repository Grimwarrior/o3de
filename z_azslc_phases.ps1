<#
.SYNOPSIS
    Runs an instrumented AZSLc over several preprocessed shaders and puts the phase breakdowns side by side.

.DESCRIPTION
    z_azslc_instrument.py adds the timers; this feeds them real inputs. The point is comparison, not a single
    number: a phase split measured on one shader says nothing about whether it holds for the shader you actually
    iterate on, and the two differ more than they look. A Material Canvas preview shader and a stock StandardPBR
    forward pass have different include sets, different line counts, and the middle end grows faster than linearly,
    so the mix shifts with size rather than staying put.

    Run it on both. If parse dominates in each, grammar work is worth doing. If the middle end climbs on the larger
    input, line reduction is still on the table alongside it.

    Preparation:
        python3 z_azslc_instrument.py          # add the timers
        <rebuild AZSLc>
        powershell -ExecutionPolicy Bypass -File .\z_azslc_phases.ps1
        python3 z_azslc_instrument.py --revert # put Main.cpp back

    The .azslin inputs are what the Asset Processor already leaves in Cache/pc. They are post-MCPP, which is what
    AZSLc actually receives, so nothing here re-runs the preprocessor or depends on it.

.EXAMPLE
    .\z_azslc_phases.ps1 -Runs 3

.EXAMPLE
    .\z_azslc_phases.ps1 -Project C:\Users\medin\O3DE\Projects\fork_t -Filter "*crystal*"
#>

param(
    [string] $Project = "C:\Users\medin\O3DE\Projects\fork_t",
    [string] $Azslc   = "",
    [string] $Filter  = "*",
    [int]    $Runs    = 3,
    [int]    $MaxShaders = 4
)

$ErrorActionPreference = "Continue"

# --full makes one invocation emit the HLSL and every reflection document together; asking for them separately
# would measure a pipeline that never runs.
# Same arguments the Shader Asset Builder passes, in the same ORDER it passes them, which matters more than it
# looks. AzslCompiler.cpp:71 starts the command line with the input path, then appends the flags, then -o.
#
#   azslc "<input>" <flags> -o "<output>"
#
# The input is positional: azslc declares it as cli.add_option("FILE", ...) with no flag, so there is no -i, and
# passing one makes CLI11 reject the whole command line (exit 109). It also has to come FIRST. --namespace is a
# std::vector option declared without an expected() limit, so CLI11 treats it as greedy and swallows any bare
# token that follows it -- put the input after the flags and it is eaten as a second namespace, leaving azslc with
# no input file to open.
$azslcArgs = @("--full", "--Zpr", "--W1", "--strip-unused-srgs", "--root-const=128", "--sc-options", "--namespace=dx")

if (-not $Azslc) {
    $candidates = @(
        Get-ChildItem -Path (Join-Path $PSScriptRoot "..") -Recurse -File -Filter "azslc.exe" -ErrorAction SilentlyContinue
        Get-ChildItem -Path (Join-Path $Project "build") -Recurse -File -Filter "azslc.exe" -ErrorAction SilentlyContinue
    ) | Sort-Object LastWriteTime -Descending
    if ($candidates.Count -eq 0) {
        Write-Host "No azslc.exe found. Build it, or pass -Azslc <path>." -ForegroundColor Red
        return
    }
    $Azslc = $candidates[0].FullName
    Write-Host "Using newest azslc.exe: $Azslc" -ForegroundColor DarkGray
    Write-Host "  built $($candidates[0].LastWriteTime)" -ForegroundColor DarkGray
}

$cache = Join-Path $Project "Cache\pc"
$inputs = @(Get-ChildItem -Path $cache -Recurse -File -Filter "*_dx12.azslin" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like $Filter } |
    Sort-Object Length -Descending |
    Select-Object -First $MaxShaders)

if ($inputs.Count -eq 0) {
    Write-Host "No *_dx12.azslin under '$cache' matching '$Filter'. Compile a graph once." -ForegroundColor Red
    return
}

# azslc.exe built from Code/Tools/ShaderCompiler links AzCore.dll, which is installed to bin\profile rather than next
# to the exe in bin\profile\Builders\AZSLc. Windows searches the exe's own folder, then system folders, then PATH --
# never the grandparent -- so launching it directly dies with STATUS_DLL_NOT_FOUND (exit -1073741515) before main()
# runs. That looks identical to "the build is not instrumented", which is why this is worth handling rather than
# leaving to the reader. Walk up from the exe to whichever folder actually holds AzCore.dll and put it on PATH.
$dllDir = $null
$probe = Split-Path $Azslc -Parent
for ($i = 0; $i -lt 5 -and $probe; $i++) {
    if (Test-Path (Join-Path $probe "AzCore.dll")) { $dllDir = $probe; break }
    $probe = Split-Path $probe -Parent
}
if ($dllDir) {
    Write-Host "  AzCore.dll found in: $dllDir" -ForegroundColor DarkGray
    $env:PATH = "$dllDir;$env:PATH"
} else {
    Write-Host "  Warning: AzCore.dll not found above the exe. If every run exits -1073741515, that is a missing DLL," -ForegroundColor Yellow
    Write-Host "  not a missing instrument. Pass -Azslc pointing at a build whose dependencies resolve." -ForegroundColor Yellow
}

$temp = Join-Path $env:TEMP "azslc_phases"
New-Item -ItemType Directory -Force -Path $temp | Out-Null

$all = @()
$shownBlocks = @()

foreach ($in in $inputs) {
    $name = $in.Name -replace "_dx12\.azslin$", ""
    $nonBlank = (Get-Content $in.FullName | Where-Object { $_.Trim() -ne "" }).Count

    Write-Host ""
    Write-Host "=================================================================" -ForegroundColor Cyan
    Write-Host $name -ForegroundColor Cyan
    Write-Host ("{0:N0} non-blank lines" -f $nonBlank) -ForegroundColor Cyan
    Write-Host "=================================================================" -ForegroundColor Cyan

    $perPhase = @{}
    $totals = @()

    for ($run = 1; $run -le $Runs; $run++) {
        $out = Join-Path $temp ("{0}_{1}" -f $name, $run)
        $errFile = "$out.stderr"
        # AZSLc writes its products beside the input when the output path is empty, which would mean writing into
        # Cache/pc and handing the Asset Processor files it did not create. Always give it a temp destination.
        $p = Start-Process -FilePath $Azslc -ArgumentList (@($in.FullName) + $azslcArgs + @("-o", "$out.hlsl")) `
                           -NoNewWindow -Wait -PassThru -RedirectStandardError $errFile -RedirectStandardOutput "$out.stdout"

        $stderr = Get-Content $errFile -ErrorAction SilentlyContinue
        if (-not ($stderr -match "AZSLc phase breakdown")) {
            if ($p.ExitCode -eq -1073741515) {
                Write-Host "  run ${run}: exit -1073741515 = STATUS_DLL_NOT_FOUND. A dependency is missing, so the process" -ForegroundColor Yellow
                Write-Host "           never started. This is not an instrumentation problem." -ForegroundColor Yellow
            } else {
                Write-Host "  run ${run}: no phase table. Is this build instrumented? (exit $($p.ExitCode))" -ForegroundColor Yellow
            }
            if ($p.ExitCode -ne 0) { $stderr | Select-Object -First 5 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray } }
            continue
        }

        # --deep and --profile-parser each add a table this script does not parse. Print them verbatim, once each,
        # from the first run that produces one: otherwise they only reach the Asset Processor log.
        foreach ($block in @("ANTLR decision profile", "SemaCheckListener callbacks")) {
            if ($shownBlocks -contains $block) { continue }
            if (-not ($stderr -match [regex]::Escape($block))) { continue }
            $inBlock = $false
            foreach ($l in $stderr) {
                if ($l -match ("=== " + [regex]::Escape($block) + " ===")) { $inBlock = $true }
                if ($inBlock) {
                    Write-Host $l -ForegroundColor DarkCyan
                    if ($l -match "^=+$" -and $l.Length -gt 20) { break }
                }
            }
            $script:shownBlocks += $block
        }

        foreach ($line in $stderr) {
            if ($line -match "^\s{2}(\S.*?)\s{2,}([\d\.]+) ms") {
                $phase = $Matches[1].Trim(); $ms = [double] $Matches[2]
                if ($phase -eq "TOTAL") { $totals += $ms }
                else {
                    if (-not $perPhase.ContainsKey($phase)) { $perPhase[$phase] = @() }
                    $perPhase[$phase] += $ms
                }
            }
        }
    }

    if ($totals.Count -eq 0) { continue }

    # Median, not mean: one run that collided with a background build should not move the number.
    function Median([double[]] $v) { $s = $v | Sort-Object; $s[[int]([math]::Floor($s.Count / 2))] }
    $medTotal = Median $totals

    $rows = $perPhase.GetEnumerator() | ForEach-Object {
        $m = Median $_.Value
        [pscustomobject]@{
            Phase   = $_.Key
            Ms      = [math]::Round($m, 1)
            Percent = [math]::Round(100.0 * $m / [math]::Max($medTotal, 1), 1)
        }
    } | Sort-Object Ms -Descending

    $rows | Format-Table Phase, Ms, Percent -AutoSize
    Write-Host ("median total {0:N0} ms over {1} run(s); spread {2:N0}-{3:N0} ms" -f `
        $medTotal, $totals.Count, ($totals | Measure-Object -Minimum).Minimum, ($totals | Measure-Object -Maximum).Maximum)
    Write-Host ("{0:N2} ms per non-blank line" -f ($medTotal / [math]::Max($nonBlank, 1)))

    $all += [pscustomobject]@{
        Shader = $name; Lines = $nonBlank; TotalMs = [math]::Round($medTotal, 0)
        MsPerLine = [math]::Round($medTotal / [math]::Max($nonBlank, 1), 3)
        Parse = ($rows | Where-Object { $_.Phase -like "*parse*" } | Select-Object -First 1).Percent
        # Sum every middle-end row. With the sub-phase instrumentation applied, "middle end" itself measures only
        # the tail after the last sub-mark and reads near zero, while "middle: reorder syms" and its siblings
        # carry the time. Taking the first row alone would under-report exactly what we are trying to see.
        Middle = [math]::Round((($rows | Where-Object { $_.Phase -like "*middle*" }) | Measure-Object Percent -Sum).Sum, 1)
    }
}

if ($all.Count -gt 1) {
    Write-Host ""
    Write-Host "=== across shaders ===" -ForegroundColor Green
    $all | Sort-Object Lines | Format-Table Shader, Lines, TotalMs, MsPerLine, Parse, Middle -AutoSize
    Write-Host "If MsPerLine climbs with Lines, the cost is superlinear and cutting lines keeps paying."
    Write-Host "If Parse holds its share across sizes, grammar work is the lever. If Middle climbs, it is not the only one."
Write-Host "Middle sums every 'middle:' row; the bare 'middle end' row is the tail after the last sub-mark and reads ~0."
}
Write-Host ""
