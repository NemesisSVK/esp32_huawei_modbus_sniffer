param(
  [string]$Root = (Get-Location).Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Add-Err([System.Collections.Generic.List[string]]$errs, [string]$msg) {
  $errs.Add($msg) | Out-Null
}

function Test-UtcStamp([string]$value) {
  return $value -match '^\d{4}-\d{2}-\d{2} \d{2}:\d{2}$'
}

function Parse-UtcMinute([string]$stamp) {
  return [DateTime]::ParseExact(
    $stamp,
    "yyyy-MM-dd HH:mm",
    [System.Globalization.CultureInfo]::InvariantCulture,
    [System.Globalization.DateTimeStyles]::AssumeUniversal -bor [System.Globalization.DateTimeStyles]::AdjustToUniversal
  )
}

function Truncate-ToMinuteUtc([datetime]$dt) {
  $u = $dt.ToUniversalTime()
  return [datetime]::new($u.Year, $u.Month, $u.Day, $u.Hour, $u.Minute, 0, [DateTimeKind]::Utc)
}

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

function Get-HandoffFingerprint([string[]]$paths) {
  $parts = @()
  foreach ($p in $paths) {
    if (-not (Test-Path -LiteralPath $p)) {
      throw "Missing file for fingerprint: $p"
    }
    $hash = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()
    $parts += "${p}:$hash"
  }
  $input = $parts -join "`n"
  return Get-StringSha256 $input
}

function Get-Ids([string]$content, [string]$pattern) {
  return [regex]::Matches($content, $pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline) |
    ForEach-Object { $_.Groups[1].Value }
}

function Get-Duplicates([string[]]$items) {
  return $items | Group-Object | Where-Object { $_.Count -gt 1 } | Select-Object -ExpandProperty Name
}

$errs = [System.Collections.Generic.List[string]]::new()
$rootPath = (Resolve-Path -LiteralPath $Root).Path
Set-Location -LiteralPath $rootPath

$requiredFiles = @(
  "AGENTS.md",
  "AI_CONTEXT.md",
  "TASKS.md",
  "DECISIONS.md",
  "SESSION_LOG.md",
  "HISTORY.md",
  "MCP_MEMORY_SYNC.json"
)

foreach ($f in $requiredFiles) {
  if (-not (Test-Path -LiteralPath $f)) {
    Add-Err $errs "Missing required file: $f"
  }
}

if ($errs.Count -gt 0) {
  $errs | ForEach-Object { Write-Error $_ }
  exit 1
}

$agents = Get-Content -LiteralPath "AGENTS.md" -Raw
$aiContext = Get-Content -LiteralPath "AI_CONTEXT.md" -Raw
$tasks = Get-Content -LiteralPath "TASKS.md" -Raw
$decisions = Get-Content -LiteralPath "DECISIONS.md" -Raw
$sessions = Get-Content -LiteralPath "SESSION_LOG.md" -Raw
$history = Get-Content -LiteralPath "HISTORY.md" -Raw
$mcpSyncRaw = Get-Content -LiteralPath "MCP_MEMORY_SYNC.json" -Raw

# Required section heading.
if ($agents -notmatch '(?m)^## AI memory maintenance(?: and Code date discipline)? \(required\)\s*$') {
  Add-Err $errs "AGENTS.md missing required section heading matching: '## AI memory maintenance (required)'"
}

# Header timestamp lines.
$stampChecks = @(
  @{ File = "AI_CONTEXT.md"; Label = "Last-Updated-UTC"; Content = $aiContext },
  @{ File = "TASKS.md"; Label = "Board-Updated-UTC"; Content = $tasks },
  @{ File = "SESSION_LOG.md"; Label = "Last-Updated-UTC"; Content = $sessions },
  @{ File = "HISTORY.md"; Label = "Last-Updated-UTC"; Content = $history }
)

foreach ($c in $stampChecks) {
  $match = [regex]::Match($c.Content, "(?m)^$($c.Label):\s*(.+)\s*$")
  if (-not $match.Success) {
    Add-Err $errs "$($c.File) missing header line: $($c.Label): YYYY-MM-DD HH:MM"
    continue
  }
  $stamp = $match.Groups[1].Value.Trim()
  if (-not (Test-UtcStamp $stamp)) {
    Add-Err $errs "$($c.File) has invalid $($c.Label) format: '$stamp'"
  }
}

# Entry UTC stamps.
$decisionUtcMatches = [regex]::Matches($decisions, '(?m)^D-\d{8}-\d{3}\s+\|\s+UTC:\s*(.+)\s*$')
foreach ($m in $decisionUtcMatches) {
  $stamp = $m.Groups[1].Value.Trim()
  if (-not (Test-UtcStamp $stamp)) {
    Add-Err $errs "DECISIONS.md has invalid UTC stamp: '$stamp'"
  }
}

$sessionUtcMatches = [regex]::Matches($sessions, '(?m)^S-\d{8}-\d{3,4}\s+\|\s+UTC:\s*(.+)\s*$')
foreach ($m in $sessionUtcMatches) {
  $stamp = $m.Groups[1].Value.Trim()
  if (-not (Test-UtcStamp $stamp)) {
    Add-Err $errs "SESSION_LOG.md has invalid UTC stamp: '$stamp'"
  }
}

$doneUtcMatches = [regex]::Matches($tasks, '(?m)Done-UTC:\s*(\d{4}-\d{2}-\d{2} \d{2}:\d{2}|\S+)')
foreach ($m in $doneUtcMatches) {
  $stamp = $m.Groups[1].Value.Trim()
  if (-not (Test-UtcStamp $stamp)) {
    Add-Err $errs "TASKS.md has invalid Done-UTC stamp: '$stamp'"
  }
}

# MCP sync state format + fingerprint alignment.
$mcpSync = $null
try {
  $mcpSync = $mcpSyncRaw | ConvertFrom-Json -ErrorAction Stop
} catch {
  Add-Err $errs "MCP_MEMORY_SYNC.json is not valid JSON: $($_.Exception.Message)"
}

$handoffPaths = @(
  "AGENTS.md",
  "AI_CONTEXT.md",
  "TASKS.md",
  "DECISIONS.md",
  "HISTORY.md",
  "SESSION_LOG.md"
)

if ($null -ne $mcpSync) {
  if (-not $mcpSync.PSObject.Properties.Name.Contains("last_sync_utc")) {
    Add-Err $errs "MCP_MEMORY_SYNC.json missing 'last_sync_utc'."
  } elseif (-not (Test-UtcStamp ([string]$mcpSync.last_sync_utc))) {
    Add-Err $errs "MCP_MEMORY_SYNC.json has invalid 'last_sync_utc' format: '$($mcpSync.last_sync_utc)'"
  }

  if (-not $mcpSync.PSObject.Properties.Name.Contains("handoff_fingerprint")) {
    Add-Err $errs "MCP_MEMORY_SYNC.json missing 'handoff_fingerprint'."
  } else {
    $currentFingerprint = Get-HandoffFingerprint $handoffPaths
    $stateFingerprint = ([string]$mcpSync.handoff_fingerprint).ToLowerInvariant()
    if ($stateFingerprint -ne $currentFingerprint) {
      Add-Err $errs "MCP memory sync state is stale: handoff fingerprint mismatch. Run scripts/update_mcp_sync_state.ps1 after MCP memory updates."
    }
  }
}

# IDs and duplicate checks.
$taskIds = Get-Ids $tasks '(?m)^(T-\d{8}-\d{3})\b'
$decisionIds = Get-Ids $decisions '(?m)^(D-\d{8}-\d{3})\b'
$sessionIds = Get-Ids $sessions '(?m)^(S-\d{8}-\d{3,4})\b'
$historyIds = Get-Ids $history '(?m)^(H-\d{8}-\d{3})\b'

foreach ($dup in (Get-Duplicates $taskIds)) { Add-Err $errs "Duplicate task ID: $dup" }
foreach ($dup in (Get-Duplicates $decisionIds)) { Add-Err $errs "Duplicate decision ID: $dup" }
foreach ($dup in (Get-Duplicates $sessionIds)) { Add-Err $errs "Duplicate session ID: $dup" }
foreach ($dup in (Get-Duplicates $historyIds)) { Add-Err $errs "Duplicate history ID: $dup" }

# Optional linked references from session log.
$linkedTaskLines = [regex]::Matches($sessions, '(?m)^Linked Tasks:\s*(.+)$')
foreach ($line in $linkedTaskLines) {
  $ids = [regex]::Matches($line.Groups[1].Value, '(T-\d{8}-\d{3})') | ForEach-Object { $_.Groups[1].Value }
  foreach ($id in $ids) {
    if ($taskIds -notcontains $id) {
      Add-Err $errs "SESSION_LOG.md references unknown task ID: $id"
    }
  }
}

$linkedDecisionLines = [regex]::Matches($sessions, '(?m)^Linked Decisions:\s*(.+)$')
foreach ($line in $linkedDecisionLines) {
  $ids = [regex]::Matches($line.Groups[1].Value, '(D-\d{8}-\d{3})') | ForEach-Object { $_.Groups[1].Value }
  foreach ($id in $ids) {
    if ($decisionIds -notcontains $id) {
      Add-Err $errs "SESSION_LOG.md references unknown decision ID: $id"
    }
  }
}

$linkedHistoryLines = [regex]::Matches($sessions, '(?m)^Linked History:\s*(.+)$')
foreach ($line in $linkedHistoryLines) {
  $ids = [regex]::Matches($line.Groups[1].Value, '(H-\d{8}-\d{3})') | ForEach-Object { $_.Groups[1].Value }
  foreach ($id in $ids) {
    if ($historyIds -notcontains $id) {
      Add-Err $errs "SESSION_LOG.md references unknown history ID: $id"
    }
  }
}

# Freshness check: relevant files must not be newer than latest S-* entry.
if ($sessionUtcMatches.Count -eq 0) {
  Add-Err $errs "SESSION_LOG.md has no valid S-* UTC entries for freshness checks."
} else {
  $latestSessionUtc = $null
  foreach ($m in $sessionUtcMatches) {
    $dt = Parse-UtcMinute($m.Groups[1].Value.Trim())
    if ($null -eq $latestSessionUtc -or $dt -gt $latestSessionUtc) {
      $latestSessionUtc = $dt
    }
  }

  $relevantCandidates = [System.Collections.Generic.List[object]]::new()
  $relevantRoots = @(
    "src",
    "scripts",
    "platformio.ini",
    "build_validate.py",
    "post_upload.py",
    "ota_flags.py",
    "README.md",
    "config.json.example",
    "ota.json.example",
    "AGENTS.md"
  )

  foreach ($root in $relevantRoots) {
    if (-not (Test-Path -LiteralPath $root)) {
      continue
    }
    $item = Get-Item -LiteralPath $root
    if ($item.PSIsContainer) {
      Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object { $relevantCandidates.Add($_) | Out-Null }
    } else {
      $relevantCandidates.Add($item) | Out-Null
    }
  }

  $excludedRelPaths = @(
    "src/BuildConfig.h",
    "AI_CONTEXT.md",
    "TASKS.md",
    "DECISIONS.md",
    "SESSION_LOG.md",
    "HISTORY.md",
    "MCP_MEMORY_SYNC.json"
  )

  $newerFiles = @()
  foreach ($file in $relevantCandidates) {
    $fullPath = [IO.Path]::GetFullPath($file.FullName)
    $relPath = $fullPath.Substring($rootPath.Length).TrimStart('\','/').Replace('\','/')
    if ($excludedRelPaths -contains $relPath) {
      continue
    }
    if ($relPath -match '^\.tmp_.*\.ps1$') {
      continue
    }

    $fileMinute = Truncate-ToMinuteUtc($file.LastWriteTimeUtc)
    if ($fileMinute -gt $latestSessionUtc) {
      $newerFiles += [PSCustomObject]@{
        Path = $relPath
        LastWriteTimeUtc = $fileMinute
      }
    }
  }

  if ($newerFiles.Count -gt 0) {
    $newest = $newerFiles | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 10
    $summary = ($newest | ForEach-Object { "$($_.Path)@$($_.LastWriteTimeUtc.ToString('yyyy-MM-dd HH:mm'))" }) -join "; "
    Add-Err $errs "Memory stale: latest session UTC is $($latestSessionUtc.ToString('yyyy-MM-dd HH:mm')), but newer relevant files exist ($summary). Add a new SESSION_LOG entry and sync memory docs."
  }
}

# Rotation warning (non-failing).
$sessionCount = $sessionIds.Count
if ($sessionCount -gt 30) {
  Write-Warning "SESSION_LOG.md has $sessionCount entries. Rotation target is 30 latest entries."
}

if ($errs.Count -gt 0) {
  $errs | ForEach-Object { Write-Error $_ }
  exit 1
}

Write-Host "AI memory validation passed."
Write-Host "Tasks: $($taskIds.Count) | Decisions: $($decisionIds.Count) | Sessions: $($sessionIds.Count) | History IDs: $($historyIds.Count)"
exit 0
