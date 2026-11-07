param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$Run,
    [switch]$Test,
    [switch]$ConfigureOnly,
    [switch]$OpenSolution,
    [switch]$Reconfigure
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$buildPath = Join-Path $projectRoot 'build'
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt'))) {
    throw "CMakeLists.txt is missing next to this script. Open the complete LunaFlow source folder: $projectRoot"
}
if ($ConfigureOnly -and ($Run -or $Test)) { throw '-ConfigureOnly cannot be combined with -Run or -Test.' }

function Normalize-ProjectPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return '' }
    return [IO.Path]::GetFullPath($Path.Replace('/', '\')).TrimEnd('\')
}
function Read-CMakeValue([string]$Cache, [string]$Name) {
    if (-not (Test-Path -LiteralPath $Cache)) { return '' }
    $match = Select-String -LiteralPath $Cache -Pattern ('^' + [regex]::Escape($Name) + ':[^=]*=(.*)$') | Select-Object -First 1
    if ($match) { return $match.Matches[0].Groups[1].Value }
    return ''
}
function Test-DependencySource([string]$Path, [string]$Header) {
    return (-not [string]::IsNullOrWhiteSpace($Path)) -and
        (Test-Path -LiteralPath (Join-Path $Path 'CMakeLists.txt')) -and
        (Test-Path -LiteralPath (Join-Path $Path $Header))
}

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakePath = $cmakeCommand.Source }
else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio Desktop development with C++ and CMake.' }
    $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakePath = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path -LiteralPath $cmakePath)) { throw 'CMake was not found. Install the C++ CMake tools component.' }

$cachePath = Join-Path $buildPath 'CMakeCache.txt'
$cachedRoot = Normalize-ProjectPath (Read-CMakeValue $cachePath 'CMAKE_HOME_DIRECTORY')
$cachedBuild = Normalize-ProjectPath (Read-CMakeValue $cachePath 'CMAKE_CACHEFILE_DIR')
$dependencies = @(
    @{ Name = 'ASSIMP'; Folder = 'assimp-src'; Header = 'include\assimp\scene.h' },
    @{ Name = 'JSON'; Folder = 'json-src'; Header = 'include\nlohmann\json.hpp' }
)
# Reuse downloaded sources, but keep them inside this project's build folder.
foreach ($dependency in $dependencies) {
    $source = Join-Path $buildPath ('_deps\' + $dependency.Folder)
    if (-not (Test-DependencySource $source $dependency.Header)) {
        $source = Read-CMakeValue $cachePath ('FETCHCONTENT_SOURCE_DIR_' + $dependency.Name)
    }
    $dependency.Source = if (Test-DependencySource $source $dependency.Header) { Normalize-ProjectPath $source } else { '' }
}

$staleCache = (Test-Path -LiteralPath $cachePath) -and
    (($cachedRoot -ine (Normalize-ProjectPath $projectRoot)) -or ($cachedBuild -ine (Normalize-ProjectPath $buildPath)))
if (($staleCache -or $Reconfigure) -and (Test-Path -LiteralPath $buildPath)) {
    $backupPath = Join-Path $projectRoot ('_build_backups\build-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    $rootPrefix = (Normalize-ProjectPath $projectRoot) + '\'
    # Verify both absolute targets before moving a directory. Preserve every old file.
    if ((Normalize-ProjectPath $buildPath) -ine ($rootPrefix + 'build') -or
        -not (Normalize-ProjectPath $backupPath).StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The build backup paths must stay inside the LunaFlow project folder.'
    }
    New-Item -ItemType Directory -Path (Split-Path $backupPath) -Force | Out-Null
    Write-Host "Recreating build files for: $projectRoot"
    Write-Host "Previous source path: $cachedRoot"
    Move-Item -LiteralPath $buildPath -Destination $backupPath
    Write-Host "Previous build preserved at: $backupPath"
    foreach ($dependency in $dependencies) {
        $prefix = (Normalize-ProjectPath $buildPath) + '\'
        if ($dependency.Source.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            $dependency.Source = Join-Path $backupPath $dependency.Source.Substring($prefix.Length)
        }
    }
    # The SDK client also keeps downloaded dependencies in build. Preserve its
    # cache on relocation so an otherwise offline rebuild does not need NuGet.
    $copilotCache = Join-Path $backupPath 'copilot-packages'
    if (Test-Path -LiteralPath $copilotCache -PathType Container) {
        New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
        Copy-Item -LiteralPath $copilotCache -Destination (Join-Path $buildPath 'copilot-packages') -Recurse
    }
}

$configureArgs = @('-S', $projectRoot, '-B', $buildPath, '-A', 'x64')
foreach ($dependency in $dependencies) {
    $destination = Join-Path $buildPath ('_deps\' + $dependency.Folder)
    if (-not (Test-DependencySource $destination $dependency.Header) -and
        (Test-DependencySource $dependency.Source $dependency.Header)) {
        New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
        Write-Host "Keeping $($dependency.Name) sources in this project..."
        Copy-Item -LiteralPath $dependency.Source -Destination $destination -Recurse
    }
    $override = if (Test-DependencySource $destination $dependency.Header) { $destination } else { '' }
    # Empty overrides let FetchContent download missing dependencies normally.
    $configureArgs += "-DFETCHCONTENT_SOURCE_DIR_$($dependency.Name):PATH=$override"
}
Write-Host "LunaFlow source: $projectRoot"
& $cmakePath @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
$solution = Get-ChildItem -LiteralPath $buildPath -File | Where-Object { $_.Name -in @('LunaFlow.slnx', 'LunaFlow.sln') } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($solution) { Write-Host "Visual Studio solution: $($solution.FullName)" }
if (-not $ConfigureOnly) {
    & $cmakePath --build $buildPath --config $Configuration --parallel 6 -- /nodeReuse:false
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}
if ($Test) {
    $ctestPath = Join-Path (Split-Path $cmakePath) 'ctest.exe'
    & $ctestPath --test-dir $buildPath -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}
if ($OpenSolution) {
    if (-not $solution) { throw 'The Visual Studio solution was not generated.' }
    Start-Process -FilePath $solution.FullName -WorkingDirectory $projectRoot
}
if ($Run) { Start-Process -FilePath (Join-Path $buildPath "$Configuration\LunaFlow.exe") -WorkingDirectory $projectRoot -WindowStyle Normal }
