param([switch]$SkipTest)
# Saves each agent's Copilot Studio web channel security secret (Direct Line) for the sign-in-free demo package.
# The directline.secret files are git-ignored and only Package-LunaFlow.ps1 copies them into the ZIP.
# Anyone who has that ZIP can call these agents, so regenerate the secrets in Copilot Studio after the demo.
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$root = $PSScriptRoot
$agents = @(
    @{ Folder = 'agent_service\copilot_client'; Name = '애니메이션 에이전트 (LunaFlow Director)' },
    @{ Folder = 'LunaFlow-VFXAgent'; Name = 'VFX 에이전트 (LunaFlow VFX Agent)' },
    @{ Folder = 'LunaFlow-HelpBot'; Name = '도우미 에이전트 (LunaFlow Helper)' })

Write-Host 'Copilot Studio > 에이전트 > 설정 > 보안 > 웹 채널 보안 > 암호 1의 [복사]를 누른 뒤 여기에 붙여넣으세요.'
Write-Host '붙여넣기: Ctrl+V 또는 마우스 오른쪽 클릭. 입력 내용은 화면에 표시되지 않습니다. Enter만 누르면 건너뜁니다.'
Write-Host '암호를 채팅·메신저·GitHub에 올리지 마세요.'
foreach ($agent in $agents) {
    $file = Join-Path $root "$($agent.Folder)\directline.secret"
    $state = if (Test-Path $file) { '저장됨' } else { '없음' }
    $secure = Read-Host -AsSecureString "$($agent.Name) 암호 [현재: $state]"
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try { $value = ([Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)).Trim() }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
    if ($value.Length -eq 0) { Write-Host '  건너뜀'; continue }
    if ($value -notmatch '^[A-Za-z0-9._\-+/=]{20,512}$') { Write-Warning '  암호 형식이 아니어서 저장하지 않았습니다. [복사] 버튼으로 다시 복사하세요.'; continue }
    [IO.File]::WriteAllText($file, $value, (New-Object Text.UTF8Encoding($false)))
    Write-Host "  저장: $($agent.Folder)\directline.secret"
}

$tracked = $null
if (Test-Path (Join-Path $root '.git')) { try { $tracked = & git -C $root ls-files -- '*directline.secret' } catch { } }
if ($tracked) { throw "directline.secret이 git에 추적되고 있습니다: $tracked. 'git rm --cached'로 제거하세요." }
if ($SkipTest) { return }

$worker = Join-Path $root 'agent_service\copilot_client\bin\Release\net10.0\CopilotConnection.exe'
if (-not (Test-Path $worker)) {
    & (Join-Path $root 'Build-CopilotClient.ps1')
    if ($LASTEXITCODE -ne 0) { throw '연결 프로그램 빌드에 실패했습니다.' }
}
Write-Host ''
Write-Host '로그인 없이 각 에이전트에 연결 확인 메시지를 보냅니다...'
$failed = 0
# Native stderr must not become a terminating error here; the exit code decides.
$ErrorActionPreference = 'Continue'
foreach ($agent in $agents) {
    $folder = Join-Path $root $agent.Folder
    if (-not (Test-Path (Join-Path $folder 'directline.secret'))) { Write-Host "[건너뜀] $($agent.Name): 암호 없음"; continue }
    $output = & $worker --config (Join-Path $folder 'settings.public.json') 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -eq 0) { Write-Host "[성공] $($agent.Name)" -ForegroundColor Green }
    else { $failed++; Write-Host "[실패] $($agent.Name)" -ForegroundColor Red }
    $output | Where-Object { $_ -match '에이전트:|오류|거부|실패|초과' } | Select-Object -First 4 | ForEach-Object { Write-Host "    $_" }
}
if ($failed -gt 0) { exit 1 }
Write-Host '이제 .\Package-LunaFlow.ps1 로 배포 ZIP을 만드세요. 시연이 끝나면 Copilot Studio에서 암호를 다시 생성하세요.'
