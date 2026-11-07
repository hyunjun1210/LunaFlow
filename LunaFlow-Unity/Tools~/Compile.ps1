param([string]$EditorPath, [string]$OutputPath)
$ErrorActionPreference = 'Stop'
if (-not $EditorPath) {
    $installed = Get-ChildItem -LiteralPath (Join-Path $env:ProgramFiles 'Unity/Hub/Editor') -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'Editor/Unity.exe') } |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $installed) { throw 'Unity Editor not found. Supply -EditorPath.' }
    $EditorPath = Join-Path $installed.FullName 'Editor'
}
$managedPath = Join-Path $EditorPath 'Data/Managed'
if (-not (Test-Path -LiteralPath $managedPath)) { throw 'Unity Data/Managed directory not found.' }
if (-not $OutputPath) { $OutputPath = Join-Path $PSScriptRoot '.build/compile' }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
& dotnet build (Join-Path $PSScriptRoot 'Compile.csproj') -c Release --ignore-failed-sources --nologo -v minimal "-p:UnityManaged=$managedPath" "-p:BaseOutputPath=$OutputPath/bin/" "-p:BaseIntermediateOutputPath=$OutputPath/obj/" "-p:MSBuildProjectExtensionsPath=$OutputPath/obj/"
if ($LASTEXITCODE -ne 0) { throw 'LunaFlow Unity C# compilation failed.' }
