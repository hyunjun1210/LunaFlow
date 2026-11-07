param(
    [Parameter(Mandatory = $true)][string]$EnvironmentId,
    [Parameter(Mandatory = $true)][string]$SchemaName,
    [string]$TenantId,
    [string]$AppClientId,
    [switch]$ReuseAnimationSignIn,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$config = Join-Path $PSScriptRoot 'settings.local.json'
if ((Test-Path -LiteralPath $config) -and -not $Force) {
    throw 'VFX settings already exist. Review them first; use -Force only to replace them.'
}
if ($ReuseAnimationSignIn) {
    if ($TenantId -or $AppClientId) { throw 'Choose either -ReuseAnimationSignIn or explicit identity IDs.' }
    $animation = Join-Path (Split-Path $PSScriptRoot) 'agent_service\copilot_client\settings.local.json'
    if (-not (Test-Path -LiteralPath $animation)) { throw 'Animation settings are missing. Supply -TenantId and -AppClientId instead.' }
    $identity = Get-Content -LiteralPath $animation -Raw | ConvertFrom-Json
    $TenantId = $identity.TenantId
    $AppClientId = $identity.AppClientId
}
foreach ($field in @(@{ Name = 'EnvironmentId'; Value = $EnvironmentId }, @{ Name = 'TenantId'; Value = $TenantId }, @{ Name = 'AppClientId'; Value = $AppClientId })) {
    $parsed = [guid]::Empty
    if (-not [guid]::TryParse($field.Value, [ref]$parsed) -or $parsed -eq [guid]::Empty) {
        throw ($field.Name + ' must be a nonempty GUID from the agent metadata or Entra app registration.')
    }
}
if ($SchemaName -cnotmatch '\A[A-Za-z0-9_]+\z') { throw 'SchemaName must be the actual agent schema name, not its display name.' }
$settings = [ordered]@{
    SuppliedEndpoint = ''
    EnvironmentId = $EnvironmentId
    SchemaName = $SchemaName
    TenantId = $TenantId
    AppClientId = $AppClientId
}
[IO.File]::WriteAllText($config, ($settings | ConvertTo-Json) + [Environment]::NewLine, (New-Object Text.UTF8Encoding($false)))
Write-Output 'Dedicated VFX settings saved. Animation settings were not changed. Run .\Test-Connection.ps1 -CheckOnly next.'
