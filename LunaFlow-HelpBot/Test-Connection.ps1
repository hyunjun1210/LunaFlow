param(
    [switch]$CheckOnly,
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
if ($CheckOnly -and $SelfTest) { throw 'Choose -CheckOnly or -SelfTest, not both.' }
$root = Split-Path $PSScriptRoot
$client = Join-Path $root 'agent_service\copilot_client\bin\Release\net10.0\CopilotConnection.exe'
$config = Join-Path $PSScriptRoot 'settings.public.json'
if (-not (Test-Path -LiteralPath $config)) { $config = Join-Path $PSScriptRoot 'settings.local.json' }
if (-not (Test-Path -LiteralPath $client)) { throw 'Build the shared SDK client first: ..\Build-CopilotClient.ps1' }
if (-not $SelfTest -and -not (Test-Path -LiteralPath $config)) { throw 'Help bot settings are missing. Add settings.public.json (sign-in free) or follow SETUP.ko.md and run Configure.ps1.' }
Write-Output "Settings: $(Split-Path -Leaf $config)"
$arguments = @('--config', $config)
if ($CheckOnly) { $arguments += '--check' }
if ($SelfTest) { $arguments += '--self-test' }
& $client @arguments
if ($LASTEXITCODE -ne 0) { throw "Copilot connection command failed (exit $LASTEXITCODE)." }
