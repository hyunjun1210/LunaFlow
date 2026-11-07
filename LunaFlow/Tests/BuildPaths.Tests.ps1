$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$fixtureRoot = Join-Path $projectRoot ('build-path-tests\' + [guid]::NewGuid().ToString('N'))
$source = Join-Path $fixtureRoot 'original source'
$relocated = Join-Path $fixtureRoot 'moved source'

function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Read-Cache([string]$Root, [string]$Name) {
    $found = Select-String -LiteralPath (Join-Path $Root 'build\CMakeCache.txt') -Pattern ('^' + [regex]::Escape($Name) + ':[^=]*=(.*)$') | Select-Object -First 1
    if ($found) { return $found.Matches[0].Groups[1].Value.Replace('/', '\') }
    return ''
}

New-Item -ItemType Directory -Path $source -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'Build-LunaFlow.ps1') -Destination $source
@'
cmake_minimum_required(VERSION 3.24)
project(LunaFlow LANGUAGES NONE)
set(recorded_dependencies "${FETCHCONTENT_SOURCE_DIR_ASSIMP};${FETCHCONTENT_SOURCE_DIR_JSON}")
'@ | Set-Content -LiteralPath (Join-Path $source 'CMakeLists.txt') -Encoding ASCII
# Minimal fixtures exercise cache/source preservation without downloading libraries.
foreach ($item in @(
    @{ Folder = 'assimp-src'; Header = 'include\assimp\scene.h' },
    @{ Folder = 'json-src'; Header = 'include\nlohmann\json.hpp' }
)) {
    $dependency = Join-Path $source ('build\_deps\' + $item.Folder)
    $header = Join-Path $dependency $item.Header
    New-Item -ItemType Directory -Path (Split-Path $header) -Force | Out-Null
    '# Dependency fixture' | Set-Content -LiteralPath (Join-Path $dependency 'CMakeLists.txt') -Encoding ASCII
    '// Dependency fixture' | Set-Content -LiteralPath $header -Encoding ASCII
}
& (Join-Path $source 'Build-LunaFlow.ps1') -ConfigureOnly
Assert ((Read-Cache $source 'CMAKE_HOME_DIRECTORY') -ieq $source) 'Initial configuration used the wrong source path.'
'preserve user data' | Set-Content -LiteralPath (Join-Path $source 'build\saved-project.txt') -Encoding ASCII
$sdkCache = Join-Path $source 'build\copilot-packages\cached-sdk\1.0.0'
New-Item -ItemType Directory -Path $sdkCache -Force | Out-Null
'offline SDK package' | Set-Content -LiteralPath (Join-Path $sdkCache 'package.marker') -Encoding ASCII

# Verify the absolute source/destination before moving the test project directory.
$fixturePrefix = [IO.Path]::GetFullPath($fixtureRoot).TrimEnd('\') + '\'
Assert ([IO.Path]::GetFullPath($source).StartsWith($fixturePrefix, [StringComparison]::OrdinalIgnoreCase)) 'Source escaped the fixture directory.'
Assert ([IO.Path]::GetFullPath($relocated).StartsWith($fixturePrefix, [StringComparison]::OrdinalIgnoreCase)) 'Destination escaped the fixture directory.'
Move-Item -LiteralPath $source -Destination $relocated
& (Join-Path $relocated 'Build-LunaFlow.ps1') -ConfigureOnly

$backups = @(Get-ChildItem -LiteralPath (Join-Path $relocated '_build_backups') -Directory)
Assert ($backups.Count -eq 1) 'A moved CMake cache was not preserved exactly once.'
Assert ((Get-Content -LiteralPath (Join-Path $backups[0].FullName 'saved-project.txt') -Raw).Trim() -eq 'preserve user data') 'The previous build lost user data.'
Assert ((Read-Cache $relocated 'CMAKE_HOME_DIRECTORY') -ieq $relocated) 'The regenerated cache still references the old source.'
Assert ((Read-Cache $relocated 'CMAKE_CACHEFILE_DIR') -ieq (Join-Path $relocated 'build')) 'The regenerated cache still references the old build folder.'
Assert ((Read-Cache $relocated 'FETCHCONTENT_SOURCE_DIR_ASSIMP') -ieq (Join-Path $relocated 'build\_deps\assimp-src')) 'Assimp sources were not recovered into the current project.'
Assert ((Read-Cache $relocated 'FETCHCONTENT_SOURCE_DIR_JSON') -ieq (Join-Path $relocated 'build\_deps\json-src')) 'JSON sources were not recovered into the current project.'
Assert ((Get-Content -LiteralPath (Join-Path $relocated 'build\copilot-packages\cached-sdk\1.0.0\package.marker') -Raw).Trim() -eq 'offline SDK package') 'Copilot SDK packages were not recovered for offline builds.'
$solutions = @(Get-ChildItem -LiteralPath (Join-Path $relocated 'build') -File | Where-Object { $_.Name -in @('LunaFlow.sln', 'LunaFlow.slnx') })
Assert ($solutions.Count -gt 0) 'The moved project has no regenerated Visual Studio solution.'

& (Join-Path $relocated 'Build-LunaFlow.ps1') -ConfigureOnly
Assert (@(Get-ChildItem -LiteralPath (Join-Path $relocated '_build_backups') -Directory).Count -eq 1) 'An unchanged cache was unnecessarily backed up again.'
Write-Host 'PASS moved source/build paths, paths with spaces, preserved user files, local dependency recovery, solution generation and repeated configuration.'
