<#
.SYNOPSIS
Puts an NPC in the town, changes its name or dialogue, has it speak, or takes it out, by hand, the way anything on
this computer can while the town is open: in OBS, or with town.ps1.

.EXAMPLE
.\npc.ps1 add sarxina
.\npc.ps1 add sarxina -Name Sarxina -Dialogue "*lurking*"
.\npc.ps1 remove sarxina
.\npc.ps1 list
#>
param(
	[Parameter(Mandatory)][ValidateSet("add", "remove", "list")][string]$What,
	[string]$Id,
	[string]$Name,
	[string]$Dialogue,
	[switch]$Speaking
)

$ErrorActionPreference = "Stop"
$town = "http://127.0.0.1:4790/npcs"

# Sends one request to the town and gives back its JSON answer, also when the status says no.
function Send-ToTown([string]$method, [string]$url, [string]$body) {
	try {
		if ($body) {
			$bytes = [Text.Encoding]::UTF8.GetBytes($body)
			return Invoke-RestMethod -Method $method -Uri $url -Body $bytes -ContentType "application/json; charset=utf-8"
		}
		return Invoke-RestMethod -Method $method -Uri $url
	} catch [System.Net.WebException] {
		if (-not $_.Exception.Response) { throw "The town is not open (nothing answers on $town): open it in OBS or with town.ps1." }
		$reader = New-Object System.IO.StreamReader($_.Exception.Response.GetResponseStream())
		return $reader.ReadToEnd() | ConvertFrom-Json
	}
}

if ($What -ne "list" -and -not $Id) { throw "Give the NPC's id." }
$url = if ($What -eq "list") { $town } else { "$town/" + [Uri]::EscapeDataString($Id) }
$method = @{ add = "PUT"; remove = "DELETE"; list = "GET" }[$What]
$fields = @{}
if ($Name) { $fields.name = $Name }
if ($Dialogue) { $fields.dialogue = $Dialogue }
if ($Speaking) { $fields.spokeAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() }
$body = if ($What -eq "add" -and $fields.Count) { $fields | ConvertTo-Json -Compress } else { "" }
Send-ToTown $method $url $body | ConvertTo-Json -Compress -Depth 4
