param()
$ErrorActionPreference = 'Stop'
$taskProject = Join-Path $PSScriptRoot 'agent_service\copilot_client\CopilotConnection.csproj'
$taskPackages = Join-Path $PSScriptRoot 'build\copilot-packages'
& dotnet restore $taskProject --packages $taskPackages --verbosity minimal
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& dotnet build $taskProject --configuration Release --no-restore --verbosity minimal
exit $LASTEXITCODE
