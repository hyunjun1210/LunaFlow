param(
    [switch]$CheckOnly,
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
$taskProject = Join-Path $PSScriptRoot 'agent_service\copilot_client\CopilotConnection.csproj'
$taskConfig = Join-Path $PSScriptRoot 'agent_service\copilot_client\settings.public.json'
if (-not (Test-Path -LiteralPath $taskConfig)) { $taskConfig = Join-Path $PSScriptRoot 'agent_service\copilot_client\settings.local.json' }
$taskPackages = Join-Path $PSScriptRoot 'build\copilot-packages'
if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) { throw '.NET 10 SDK가 필요합니다.' }
if (-not $SelfTest -and -not (Test-Path -LiteralPath $taskConfig)) {
    throw 'agent_service\copilot_client에 settings.public.json(로그인 없이) 또는 settings.local.json(settings.example.json 복사)을 만드세요.'
}
& dotnet restore $taskProject --packages $taskPackages --verbosity minimal
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& dotnet build $taskProject --configuration Release --no-restore --verbosity minimal
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$taskArguments = @('--config', $taskConfig)
if ($SelfTest) { $taskArguments += '--self-test' }
if ($CheckOnly) { $taskArguments += '--check' }
& dotnet run --project $taskProject --configuration Release --no-build --no-restore -- @taskArguments
exit $LASTEXITCODE
