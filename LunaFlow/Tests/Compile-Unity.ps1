param([string]$EditorPath)
$ErrorActionPreference='Stop'
if (-not $EditorPath) {
    $editorRoot=Join-Path $env:ProgramFiles 'Unity\Hub\Editor'
    $installed=Get-ChildItem -LiteralPath $editorRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
    if (-not $installed) { throw 'Unity Editor was not found. Pass -EditorPath with its Editor folder.' }
    $EditorPath=Join-Path $installed.FullName 'Editor'
}
$managedPath=Join-Path $EditorPath 'Data\Managed'
if (-not (Test-Path -LiteralPath $managedPath)) { throw 'The Unity Managed folder was not found.' }
$workspacePath=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$outputPath=Join-Path $workspacePath 'build\unity-csharp'
$env:DOTNET_CLI_TELEMETRY_OPTOUT='1'
& dotnet build (Join-Path $PSScriptRoot 'UnityIntegration.csproj') -c Release --ignore-failed-sources --nologo -v minimal "-p:UnityManaged=$managedPath" "-p:BaseOutputPath=$outputPath\bin\" "-p:BaseIntermediateOutputPath=$outputPath\obj\" "-p:MSBuildProjectExtensionsPath=$outputPath\obj\"
if ($LASTEXITCODE -ne 0) { throw 'Unity integration C# compilation failed.' }
