param(
  [string]$Root = (Get-Location).Path,
  [string]$StateFile = "MCP_MEMORY_SYNC.json",
  [string]$SessionId = "",
  [string]$Notes = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-StringSha256([string]$text) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($text)
    $hashBytes = $sha.ComputeHash($bytes)
    return ([BitConverter]::ToString($hashBytes)).Replace("-", "").ToLowerInvariant()
  } finally {
    $sha.Dispose()
  }
}

$rootPath = (Resolve-Path -LiteralPath $Root).Path
Set-Location -LiteralPath $rootPath

$sourceFiles = @(
  "AGENTS.md",
  "AI_CONTEXT.md",
  "TASKS.md",
  "DECISIONS.md",
  "HISTORY.md",
  "SESSION_LOG.md"
)

$fileHashes = @()
foreach ($path in $sourceFiles) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing required handoff file for MCP sync state: $path"
  }
  $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
  $fileHashes += [PSCustomObject]@{
    path = $path
    sha256 = $hash
  }
}

$fingerprintInput = ($fileHashes | ForEach-Object { "$($_.path):$($_.sha256)" }) -join "`n"
$fingerprint = Get-StringSha256 $fingerprintInput
$utcNow = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd HH:mm")

$state = [ordered]@{
  schema_version = 1
  project = "esp32_modbus_sniffer"
  last_sync_utc = $utcNow
  handoff_fingerprint = $fingerprint
  source_files = $fileHashes
}

if ($SessionId -ne "") {
  $state.last_session_id = $SessionId
}
if ($Notes -ne "") {
  $state.notes = $Notes
}

$json = $state | ConvertTo-Json -Depth 6
Set-Content -LiteralPath $StateFile -Value $json -Encoding UTF8

Write-Host "MCP memory sync state updated:"
Write-Host "  File: $StateFile"
Write-Host "  UTC: $utcNow"
Write-Host "  Fingerprint: $fingerprint"
