<#
.SYNOPSIS
Opens the town in your browser without OBS: serves the game folder at http://127.0.0.1:4790/, with the same trigger
and feed the plugin has (npc.ps1 works against it), and opens that page. Closing this window closes the town.

.DESCRIPTION
Needs the game's engine and art (game\get-engine-files.ps1) and a build (BUILDING.md), which makes
chatsaga-town.exe. While OBS runs ChatSaga, the town is at the same address already, and this isn't needed.
#>
$ErrorActionPreference = "Stop"
$repository = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$program = Join-Path $repository "build_x64\plugins\chatsaga\RelWithDebInfo\chatsaga-town.exe"
$game = Join-Path $PSScriptRoot "game"

if (-not (Test-Path $program)) { throw "Build first (BUILDING.md): $program isn't there." }
if (-not (Test-Path (Join-Path $game "js\rmmz_core.js"))) { throw "Run game\get-engine-files.ps1 first." }

# The program uses the Qt that the build downloaded, beside the OBS sources.
$qt = Get-ChildItem (Join-Path $repository ".deps") -Directory -Filter "obs-deps-qt6-*" | Select-Object -First 1
$env:PATH = (Join-Path $qt.FullName "bin") + ";" + $env:PATH

$town = Start-Process $program -ArgumentList "`"$game`"" -NoNewWindow -PassThru
Start-Sleep -Milliseconds 500
if ($town.HasExited) { throw "The town didn't open (is OBS already running it on port 4790?)." }
Start-Process "http://127.0.0.1:4790/"
Write-Host "The town is open at http://127.0.0.1:4790/. Close this window (or press Ctrl+C) to close it."
try { $town.WaitForExit() } finally { if (-not $town.HasExited) { $town.Kill() } }
