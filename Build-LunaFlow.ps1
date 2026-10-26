param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$Run,
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakePath = $cmakeCommand.Source }
else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio Desktop development with C++ and CMake.' }
    $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakePath = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path -LiteralPath $cmakePath)) { throw 'CMake was not found. Install the C++ CMake tools component.' }
$buildPath = Join-Path $PSScriptRoot 'build'
& $cmakePath -S $PSScriptRoot -B $buildPath -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmakePath --build $buildPath --config $Configuration --parallel 6 -- /nodeReuse:false
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if ($Test) {
    $ctestPath = Join-Path (Split-Path $cmakePath) 'ctest.exe'
    & $ctestPath --test-dir $buildPath -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}
if ($Run) { Start-Process -FilePath (Join-Path $buildPath "$Configuration\LunaFlow.exe") -WorkingDirectory $PSScriptRoot -WindowStyle Normal }
