param(
    [switch]$SkipBuild,
    [string]$Output = (Join-Path $PSScriptRoot 'dist')
)
# Builds a portable, sign-in-free LunaFlow demo: copy the folder (or zip) to any Windows 10/11 x64 PC and run LunaFlow.exe.
# No .NET or Visual C++ install is needed. AI agents connect through settings.public.json (Copilot Studio "No authentication").
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$release = Join-Path $root 'build\Release'
$package = Join-Path $Output 'LunaFlow-Demo'
$zip = Join-Path $Output 'LunaFlow-Demo.zip'

if (-not $SkipBuild) {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    $cmake = if ($cmake) { $cmake.Source } else { 'C:\Program Files\CMake\bin\cmake.exe' }
    & $cmake --build (Join-Path $root 'build') --config Release --target LunaFlow -- /nologo /v:m /nodeReuse:false
    if ($LASTEXITCODE -ne 0) { throw 'LunaFlow build failed. Close LunaFlow.exe if it is running, then retry.' }
}
if (-not (Test-Path (Join-Path $release 'LunaFlow.exe'))) { throw "build\Release\LunaFlow.exe not found. Run Build-LunaFlow.ps1 first." }

$agents = @(
    @{ Folder = 'agent_service\copilot_client'; Name = 'animation' },
    @{ Folder = 'LunaFlow-VFXAgent'; Name = 'VFX' },
    @{ Folder = 'LunaFlow-HelpBot'; Name = 'help' })
foreach ($agent in $agents) {
    $settings = Join-Path $root "$($agent.Folder)\settings.public.json"
    if (-not (Test-Path $settings)) { throw "Missing $($agent.Folder)\settings.public.json for the $($agent.Name) agent." }
}

if (Test-Path $package) { Remove-Item $package -Recurse -Force }
New-Item -ItemType Directory -Force -Path $package | Out-Null
Write-Output 'Step 1/5: Copying the editor and its assets'
Copy-Item (Join-Path $release 'LunaFlow.exe') $package
foreach ($folder in 'Characters', 'Fonts', 'HelpBot', 'licenses', 'Shaders', 'Unity') {
    $source = Join-Path $release $folder
    if (Test-Path $source) { Copy-Item $source (Join-Path $package $folder) -Recurse }
}

Write-Output 'Step 2/5: Adding the Visual C++ runtime next to LunaFlow.exe'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found. Install Visual Studio with the C++ workload.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC') -Directory | Where-Object Name -Match '^\d+\.\d+' |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Get-ChildItem (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC14*.CRT' -ErrorAction SilentlyContinue } |
    Select-Object -First 1
if (-not $crt) { throw 'Visual C++ x64 redistributable files not found under the Visual Studio installation.' }
Copy-Item (Join-Path $crt.FullName '*.dll') $package

Write-Output 'Step 3/5: Publishing the self-contained AI connection worker'
$worker = Join-Path $package 'agent_service\copilot_client\bin\Release\net10.0'
& dotnet publish (Join-Path $root 'agent_service\copilot_client\CopilotConnection.csproj') --configuration Release --runtime win-x64 --self-contained true `
    --output $worker --verbosity quiet "-p:RestorePackagesPath=$(Join-Path $root 'build\copilot-packages')" `
    -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true -p:DebugType=None -p:DebugSymbols=false
if ($LASTEXITCODE -ne 0) { throw 'dotnet publish failed.' }
& (Join-Path $worker 'CopilotConnection.exe') --self-test
if ($LASTEXITCODE -ne 0) { throw 'The published worker failed its offline self-test.' }

Write-Output 'Step 4/5: Adding sign-in-free agent settings, webcam mocap setup files and the guide'
$secrets = @()
foreach ($agent in $agents) {
    $target = Join-Path $package $agent.Folder
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    # Only the public settings ship; settings.local.json and sign-in caches stay on this PC.
    Copy-Item (Join-Path $root "$($agent.Folder)\settings.public.json") $target
    # Optional web channel secret (Set-DemoSecrets.ps1); without it the agent's public token endpoint is used.
    $secret = Join-Path $root "$($agent.Folder)\directline.secret"
    if (Test-Path $secret) { Copy-Item $secret $target; $secrets += $agent.Name }
    else { Write-Warning "No directline.secret for the $($agent.Name) agent: it needs Copilot Studio 'No authentication' token access." }
}
$mocap = Join-Path $package 'LunaFlow-Mocap'
New-Item -ItemType Directory -Force -Path $mocap | Out-Null
foreach ($item in 'Tracker', 'Models', 'Setup.ps1', 'requirements.txt', 'README.ko.md', 'THIRD_PARTY.md') {
    $source = Join-Path $root "LunaFlow-Mocap\$item"
    if (Test-Path $source) { Copy-Item $source (Join-Path $mocap $item) -Recurse }
}
Get-ChildItem $mocap -Recurse -Directory -Filter '__pycache__' | Remove-Item -Recurse -Force
Copy-Item (Join-Path $root 'LunaFlow\Docs\Demo_README.ko.txt') (Join-Path $package 'README_KO.txt')
$leaked = Get-ChildItem $package -Recurse -Force | Where-Object { $_.Name -in 'settings.local.json', '.auth', 'auth', '.venv' -or $_.Extension -eq '.msal' }
if ($leaked) { throw "Private files must not ship: $($leaked.FullName -join ', ')" }

Write-Output 'Step 5/5: Creating the zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($package, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
$size = (Get-Item $zip).Length / 1MB
Write-Output ("Done: {0} ({1:N1} MB)" -f $zip, $size)
if ($secrets) {
    Write-Warning ("This ZIP contains Direct Line secrets for: {0}. Share it only for the demo, not in the public GitHub repo; regenerate the secrets in Copilot Studio afterwards." -f ($secrets -join ', '))
}
