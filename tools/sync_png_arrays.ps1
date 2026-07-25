<#
.SYNOPSIS
    Re-generates the embedded PNG byte arrays in source/pngfiles.cpp from the
    PNG files on disk.

.DESCRIPTION
    Editor icons are compiled into the executable as byte arrays in
    source/pngfiles.cpp - nothing is loaded from brushes/ or icons/ at runtime.
    Editing a .png therefore has NO effect until its array is regenerated and
    the solution is rebuilt. This script does the regeneration.

    Source file -> symbol mapping:
        brushes\<name>.png -> <name>_png
        icons\<name>.png   -> icon_<name>_png

.PARAMETER Name
    One or more PNG files to sync (path relative to the repo root, or just the
    file name). If omitted, every drifted array is reported but nothing is
    written unless -All is given.

.PARAMETER All
    Sync every array whose embedded bytes differ from the file on disk.

.EXAMPLE
    .\tools\sync_png_arrays.ps1                       # report drift only
    .\tools\sync_png_arrays.ps1 brushes\door_locked.png
    .\tools\sync_png_arrays.ps1 -All
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]] $Name,
    [switch] $All
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cppPath = Join-Path $root "source\pngfiles.cpp"
$hdrPath = Join-Path $root "source\pngfiles.h"

# Build symbol -> source-file map from the declarations in pngfiles.h.
$decl = @{}
Select-String -Path $hdrPath -Pattern '^extern unsigned char ([a-z0-9_]+)\[(\d+)\];' | ForEach-Object {
    $decl[$_.Matches[0].Groups[1].Value] = [int]$_.Matches[0].Groups[2].Value
}

$entries = @()
foreach ($sym in $decl.Keys) {
    if ($sym -match '^icon_(.+)_png$') {
        $rel = "icons\$($Matches[1]).png"
    } else {
        $base = $sym -replace '_png$', ''
        # unprefixed symbols normally come from brushes/, but a few live
        # elsewhere (editor_icon under brushes/icon/, exclamation under icons/)
        $rel = $null
        foreach ($dir in @("brushes", "brushes\icon", "icons")) {
            $candidate = "$dir\$base.png"
            if (Test-Path (Join-Path $root $candidate)) { $rel = $candidate; break }
        }
        if (-not $rel) { $rel = "brushes\$base.png" }
    }
    $full = Join-Path $root $rel
    if (-not (Test-Path $full)) { Write-Warning "no source file for $sym (expected $rel)"; continue }
    $entries += [pscustomobject]@{
        Symbol = $sym; Rel = $rel; Full = $full
        FileLen = (Get-Item $full).Length; ArrayLen = $decl[$sym]
    }
}

$drifted = $entries | Where-Object { $_.FileLen -ne $_.ArrayLen }

if ($Name) {
    $wanted = $Name | ForEach-Object { (Split-Path $_ -Leaf) }
    $targets = $entries | Where-Object { (Split-Path $_.Rel -Leaf) -in $wanted }
    $missing = $wanted | Where-Object { $_ -notin ($targets | ForEach-Object { Split-Path $_.Rel -Leaf }) }
    if ($missing) { throw "not embedded resources: $($missing -join ', ')" }
} elseif ($All) {
    $targets = $drifted
} else {
    if ($drifted) {
        "Arrays that differ from their source file:"
        $drifted | ForEach-Object { "  {0,-34} file={1,6}  embedded={2,6}" -f $_.Rel, $_.FileLen, $_.ArrayLen }
        ""
        "Nothing written. Pass a file name to sync one, or -All to sync every entry above."
    } else {
        "All embedded arrays match their source files."
    }
    return
}

if (-not $targets) { "Nothing to do."; return }

$cpp = Get-Content $cppPath -Raw
$hdr = Get-Content $hdrPath -Raw

foreach ($t in $targets) {
    $bytes = [System.IO.File]::ReadAllBytes($t.Full)
    $len = $bytes.Length
    $sb = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt $len; $i += 8) {
        $chunk = $bytes[$i..([Math]::Min($i + 7, $len - 1))]
        $line = ($chunk | ForEach-Object { "0x{0:x2}" -f $_ }) -join ", "
        if ($i + 8 -lt $len) { $line += "," }
        [void]$sb.AppendLine("`t$line")
    }
    $repl = "unsigned char $($t.Symbol)[$len] = {`n" + $sb.ToString().TrimEnd() + "`n};"
    $m = [regex]::Match($cpp, "unsigned char $($t.Symbol)\[\d+\] = \{.*?\};", 'Singleline')
    if (-not $m.Success) { throw "array $($t.Symbol) not found in pngfiles.cpp" }
    $cpp = $cpp.Remove($m.Index, $m.Length).Insert($m.Index, $repl)

    $leaf = Split-Path $t.Rel -Leaf
    $cpp = $cpp -replace ("/\* " + [regex]::Escape($leaf) + " - \d+ bytes \*/"), "/* $leaf - $len bytes */"
    $hdr = $hdr -replace "extern unsigned char $($t.Symbol)\[\d+\];", "extern unsigned char $($t.Symbol)[$len];"

    "synced {0,-34} {1} -> {2} bytes" -f $t.Rel, $t.ArrayLen, $len
}

Set-Content $cppPath -Value $cpp -Encoding UTF8 -NoNewline
Set-Content $hdrPath -Value $hdr -Encoding UTF8 -NoNewline
""
"Rebuild for the change to reach the executable:"
"  msbuild vcproj\Editor\Editor.sln /p:Configuration=Release /p:Platform=x64 /m"
"  msbuild vcproj\MapServer\MapServer.sln /p:Configuration=Release /p:Platform=x64 /m"
