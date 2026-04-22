param(
    [string]$Source = "C:\Users\NeMeSiS\!Projects\esp32_modbus_sniffer",
    [switch]$NoGitStatus
)

$ErrorActionPreference = 'Stop'
$Mirror = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not (Test-Path -LiteralPath $Source)) {
    throw "Source path not found: $Source"
}

$excludeDirs = @('.git', '.pio', '__pycache__', 'captures', '_supporting-projects')
$excludeFiles = @('config.json', 'data\\config.json', 'ota.json', 'data\\ota.json', 'compile_commands.json', 'src\\BuildConfig.h')

$robocopyArgs = @($Source, $Mirror, '/MIR', '/R:2', '/W:1')
$robocopyArgs += ($excludeDirs | ForEach-Object { @('/XD', $_) })
$robocopyArgs += ($excludeFiles | ForEach-Object { @('/XF', $_) })

& robocopy @robocopyArgs | Out-Host
$rc = $LASTEXITCODE
if ($rc -ge 8) {
    throw "robocopy failed with exit code $rc"
}

# Explicit safety cleanup for path-specific exclusions
foreach ($rel in $excludeFiles) {
    $p = Join-Path $Mirror $rel
    if (Test-Path -LiteralPath $p) {
        Remove-Item -LiteralPath $p -Force
        Write-Output "removed:$rel"
    }
}

Write-Output "sync_complete (robocopy_exit=$rc)"

if (-not $NoGitStatus) {
    git -C $Mirror status --short
}
