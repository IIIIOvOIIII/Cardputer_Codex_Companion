param(
    [Parameter(Mandatory = $true)]
    [string]$Template,
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = "Stop"
$taskName = "Cardputer Codex Companion"
$userID = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$workingDirectory = Split-Path -Parent $Executable

function Escape-XML([string]$Value) {
    return [System.Security.SecurityElement]::Escape($Value)
}

$taskXML = Get-Content -LiteralPath $Template -Raw
$taskXML = $taskXML.Replace("__USER_ID__", (Escape-XML $userID))
$taskXML = $taskXML.Replace("__AGENT_EXE__", (Escape-XML $Executable))
$taskXML = $taskXML.Replace(
    "__WORKING_DIRECTORY__",
    (Escape-XML $workingDirectory)
)

Register-ScheduledTask -TaskName $taskName -Xml $taskXML -Force | Out-Null
