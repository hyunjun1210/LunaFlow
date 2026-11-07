param([switch]$SkipModel, [switch]$SmokeTest)
$ErrorActionPreference = 'Stop'
# Invoke-WebRequest progress rendering slows Windows PowerShell 5 downloads by orders of magnitude.
$ProgressPreference = 'SilentlyContinue'
Write-Output 'Step 1/4: Preparing Python 3.11 environment'
$runtime = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $runtime)) {
    & py -3.11 -c "import struct,sys; assert sys.version_info[:2]==(3,11) and struct.calcsize('P')==8, 'Python 3.11 x64 required'"
    if ($LASTEXITCODE -ne 0) { throw 'Install Python 3.11 x64 with the Windows Python launcher.' }
    & py -3.11 -m venv (Join-Path $PSScriptRoot '.venv')
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create isolated mocap environment.' }
}
Write-Output 'Step 2/4: Installing MediaPipe and OpenCV (first run downloads about 100 MB)'
& $runtime -m pip install --disable-pip-version-check -r (Join-Path $PSScriptRoot 'requirements.txt')
if ($LASTEXITCODE -ne 0) { throw 'Mocap dependency installation failed.' }
& $runtime -m pip check
if ($LASTEXITCODE -ne 0) { throw 'Mocap dependency check failed.' }
$model = Join-Path $PSScriptRoot 'Models\pose_landmarker_lite.task'
$hash = '59929e1d1ee95287735ddd833b19cf4ac46d29bc7afddbbf6753c459690d574a'
if (-not $SkipModel) {
    Write-Output 'Step 3/4: Downloading and verifying pose model'
    New-Item -ItemType Directory -Force -Path (Split-Path $model) | Out-Null
    if (-not (Test-Path -LiteralPath $model)) {
        $download = "$model.download"
        Invoke-WebRequest -UseBasicParsing -Uri 'https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task' -OutFile $download
        if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ine $hash) { throw 'Model checksum mismatch; refusing installation.' }
        Move-Item -LiteralPath $download -Destination $model
    }
    if ((Get-FileHash -LiteralPath $model -Algorithm SHA256).Hash -ine $hash) { throw 'Existing model checksum mismatch; obtain the documented version.' }
}
if ($SmokeTest) {
    Write-Output 'Step 4/4: Testing model inference'
    & $runtime -I (Join-Path $PSScriptRoot 'Tracker\tracker.py') --model $model --smoke-test
    if ($LASTEXITCODE -ne 0) { throw 'Model inference smoke test failed.' }
}
Write-Output 'Mocap ready. No camera was opened. Use View > Webcam motion capture in LunaFlow.'
