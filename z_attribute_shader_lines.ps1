# Attribute the lines of a preprocessed shader (.azslin) back to the files they came from.
#
# This is how to check what a Definitions block actually removed, rather than what it was expected to remove.
# Twice in this project a define was set correctly and the file it was supposed to eliminate was still in the
# output -- Debug.azsli, which is included unconditionally, and NVLC.azsli, which reached the shader through
# an unguarded include. Neither showed up as an error anywhere. See the "Verifying this table" section of
# Gems/Atom/Tools/MaterialCanvas/Assets/MaterialCanvas/Pipelines/PreviewOnly/ShaderTemplates/README.md.
#
# MCPP leaves line markers behind ('# <line> "<file>"' or '#line <line> "<file>"'), so every content line
# between two markers belongs to the file the last marker named. Blank lines are not counted: expanding an
# include out leaves long runs of them and they are not work azslc has to do.

param(
    [Parameter(Mandatory = $true)][string] $AzslinPath,
    [int] $Top = 30
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $AzslinPath)) { throw "input not found: $AzslinPath" }

$markerPattern = '^\s*#\s*(?:line\s+)?\d+\s+"(.*?)"'
$counts = @{}
$current = $null
$total = 0

foreach ($line in [System.IO.File]::ReadLines((Resolve-Path $AzslinPath))) {
    $match = [System.Text.RegularExpressions.Regex]::Match($line, $markerPattern)
    if ($match.Success) {
        $current = $match.Groups[1].Value -replace '\\', '/'
        continue
    }
    if ($null -eq $current) { continue }
    if ($line.Trim().Length -eq 0) { continue }
    if ($counts.ContainsKey($current)) { $counts[$current]++ } else { $counts[$current] = 1 }
    $total++
}

Write-Host ("{0}" -f (Split-Path -Leaf $AzslinPath))
Write-Host ("{0:N0} content lines, {1} files`n" -f $total, $counts.Count)

if ($total -eq 0) {
    Write-Host "No line markers found. Is this really a preprocessed .azslin?" -ForegroundColor Yellow
    return
}

"{0,7} {1,7}  {2}" -f "lines", "share", "file"
$counts.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First $Top | ForEach-Object {
    # Trim the engine prefixes so the interesting part of the path is what lines up in the column.
    $short = $_.Key -replace '.*/ShaderLib/', '' -replace '.*/Assets/', ''
    "{0,7:N0} {1,6:N1}%  {2}" -f $_.Value, (100 * $_.Value / $total), $short
}

Write-Host ""
Write-Host "Absent files are the point: a define that worked removes its file from this list entirely."
