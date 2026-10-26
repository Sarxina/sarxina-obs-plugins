<#
.SYNOPSIS
Copies what the town needs from the RPG Maker MZ install into this project, which keeps only its own files.

.DESCRIPTION
The repository holds the game's data, its plugin and its project file. The engine, RPG Maker's sounds and pictures,
and the Time Fantasy art the town is drawn with come from the install, the same way the editor puts them in a new
project. Run this once after cloning, and again after RPG Maker MZ updates. Nothing in the install is changed.

.PARAMETER RpgMaker
The RPG Maker MZ folder. Found in the Steam libraries when left out.
#>
param([string]$RpgMaker)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot

# Every Steam library on the PC: Steam's own folder and the ones its libraryfolders.vdf lists.
function Get-SteamLibraries {
	$steam = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
	if (-not $steam) { $steam = "${env:ProgramFiles(x86)}\Steam" }
	$libraries = @($steam)
	$vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
	if (Test-Path $vdf) {
		$libraries += Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' |
			ForEach-Object { $_.Matches[0].Groups[1].Value -replace '\\\\', '\' }
	}
	return $libraries | Select-Object -Unique
}

# The RPG Maker MZ folder: the one given, or the first Steam library that has it.
function Find-RpgMaker {
	if ($RpgMaker) { return $RpgMaker }
	foreach ($library in Get-SteamLibraries) {
		$folder = Join-Path $library "steamapps\common\RPG Maker MZ"
		if (Test-Path (Join-Path $folder "newdata")) { return $folder }
	}
	throw "RPG Maker MZ was not found in Steam's libraries. Give its folder with -RpgMaker."
}

# Copies the files under $from into $to, keeping their folders, replacing what is there.
function Copy-Into([string]$from, [string]$to) {
	New-Item -ItemType Directory -Force $to | Out-Null
	Copy-Item -Path (Join-Path $from "*") -Destination $to -Recurse -Force
}

$install = Find-RpgMaker
$template = Join-Path $install "newdata"
$timeFantasy = Join-Path $install "dlc\Time Fantasy"
if (-not (Test-Path $timeFantasy)) { throw "The Time Fantasy DLC is not in $install\dlc." }

# The engine and RPG Maker's own sounds and pictures, as in a new project.
Copy-Item (Join-Path $template "index.html") $project -Force
foreach ($folder in "css", "effects", "fonts", "icon", "img", "audio", "js\libs") {
	Copy-Into (Join-Path $template $folder) (Join-Path $project $folder)
}
Copy-Item (Join-Path $template "js\main.js") (Join-Path $project "js") -Force
Copy-Item (Join-Path $template "js\rmmz_*.js") (Join-Path $project "js") -Force

# Time Fantasy: the town's tiles and the characters NPCs are dressed from.
Copy-Into (Join-Path $timeFantasy "tilesets") (Join-Path $project "img\tilesets")
Copy-Into (Join-Path $timeFantasy "characters") (Join-Path $project "img\characters")
# From its expansion: more trees for the town, more people to dress NPCs as, and the town's cats and dogs.
Copy-Item (Join-Path $timeFantasy "expansion\bonus_trees.png") (Join-Path $project "img\tilesets") -Force
foreach ($sheet in "bonus1", "animals1") {
	Copy-Item (Join-Path $timeFantasy "expansion\$sheet.png") (Join-Path $project "img\characters") -Force
}

Write-Host "Copied the engine and the art from $install"
