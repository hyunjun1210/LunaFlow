param([string]$EditorPath, [string]$ProjectPath, [switch]$NoGraphics, [switch]$SkipAssemblyUpdater)
$ErrorActionPreference = 'Stop'
if (-not $EditorPath) {
    $installed = Get-ChildItem -LiteralPath (Join-Path $env:ProgramFiles 'Unity/Hub/Editor') -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'Editor/Unity.exe') } |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $installed) { throw 'Unity Editor not found. Supply -EditorPath.' }
    $EditorPath = Join-Path $installed.FullName 'Editor'
}
$packagePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $ProjectPath) { $ProjectPath = Join-Path $packagePath '.test-project' }
$ProjectPath = [IO.Path]::GetFullPath($ProjectPath)
if ($ProjectPath.Contains(' ')) { throw 'Use a scratch ProjectPath without spaces for Unity command-line verification.' }
$marker = Join-Path $ProjectPath '.lunaflow-test-project'
if ((Test-Path -LiteralPath $ProjectPath) -and -not (Test-Path -LiteralPath $marker)) {
    throw 'Use a new empty scratch directory. This script will not overwrite an existing Unity project.'
}
foreach ($folder in @('Assets/Editor','Packages','ProjectSettings')) { New-Item -ItemType Directory -Path (Join-Path $ProjectPath $folder) -Force | Out-Null }
[IO.File]::WriteAllText($marker, 'Isolated LunaFlow importer verification project.')
$dependencies = [ordered]@{
    'com.lunaflow.unity-importer' = 'file:' + $packagePath.Replace('\','/')
    'com.unity.modules.animation' = '1.0.0'
    'com.unity.modules.imageconversion' = '1.0.0'
    'com.unity.modules.jsonserialize' = '1.0.0'
    'com.unity.modules.particlesystem' = '1.0.0'
    'com.unity.modules.physics' = '1.0.0'
    'com.unity.modules.imgui' = '1.0.0'
}
[IO.File]::WriteAllText((Join-Path $ProjectPath 'Packages/manifest.json'), (@{ dependencies = $dependencies } | ConvertTo-Json -Depth 4))
$version = Split-Path (Split-Path $EditorPath -Parent) -Leaf
[IO.File]::WriteAllText((Join-Path $ProjectPath 'ProjectSettings/ProjectVersion.txt'), "m_EditorVersion: $version`n")
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'UnityVerification.cs') -Destination (Join-Path $ProjectPath 'Assets/Editor/UnityVerification.cs') -Force
$logPath = Join-Path $ProjectPath 'verification.log'
$resultPath = Join-Path $ProjectPath 'LunaFlowVerification-result.json'
[IO.File]::WriteAllText($resultPath, '{"success":false,"pending":true}')
$arguments = @('-batchmode','-projectPath',$ProjectPath,'-executeMethod','LunaFlowUnityVerification.Run','-logFile',$logPath)
if ($NoGraphics) { $arguments += '-nographics' }
if ($SkipAssemblyUpdater) { $arguments += '-disable-assembly-updater' }
$process = Start-Process -FilePath (Join-Path $EditorPath 'Unity.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
Write-Output "Unity PID: $($process.Id); log: $logPath"
$process.WaitForExit()
if (-not (Test-Path -LiteralPath $resultPath) -or $process.ExitCode -ne 0) {
    Get-Content -LiteralPath $logPath -Tail 45
    throw 'Unity verification failed. Inspect verification.log.'
}
$result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
if (-not $result.success) { throw 'Unity verification reported failure.' }
Get-Content -LiteralPath $resultPath
