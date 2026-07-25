<#
.SYNOPSIS
    Re-embeds every editor image whose PNG on disk differs from the byte array
    compiled into the executable.

.DESCRIPTION
    Convenience wrapper around sync_png_arrays.ps1 that runs with -All, i.e. it
    WRITES by default instead of only reporting. Use this as the pre-build step
    after editing any art under brushes\ or icons\:

        .\tools\sync_all_png_arrays.ps1
        msbuild vcproj\Editor\Editor.sln /p:Configuration=Release /p:Platform=x64 /m

    Nothing is read from brushes\ or icons\ at runtime - every image lives in
    source\pngfiles.cpp - so editing a .png has no effect until this runs and
    the solution is rebuilt.

    Run .\tools\sync_png_arrays.ps1 (no arguments) instead if you only want to
    see what has drifted without changing anything.

.PARAMETER DryRun
    Report what would be re-embedded, but write nothing. Equivalent to calling
    sync_png_arrays.ps1 with no arguments.

.EXAMPLE
    .\tools\sync_all_png_arrays.ps1
    Re-embeds every drifted image.

.EXAMPLE
    .\tools\sync_all_png_arrays.ps1 -DryRun
    Lists drifted images without touching pngfiles.cpp.
#>
[CmdletBinding()]
param(
    [switch] $DryRun
)

$ErrorActionPreference = 'Stop'

$inner = Join-Path $PSScriptRoot 'sync_png_arrays.ps1'
if (-not (Test-Path $inner)) {
    throw "sync_png_arrays.ps1 not found next to this script (expected $inner)"
}

# Delegate to the single implementation so the mapping rules and the array
# rewriting logic only exist in one place.
if ($DryRun) {
    & $inner
} else {
    & $inner -All
}
