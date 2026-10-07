param(
    [Parameter(Mandatory=$true)][string]$ProbeBinary,
    [Parameter(Mandatory=$true)][string]$QtBinDirectory,
    [string]$InputManifest = (Join-Path $PSScriptRoot 'native-editing-performance-diagnostic.json'),
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
foreach ($taskInput in @($ProbeBinary,$InputManifest)) {
    if (-not (Test-Path -LiteralPath $taskInput -PathType Leaf)) { throw "Missing input: $taskInput" }
}
if (-not (Test-Path -LiteralPath (Join-Path $QtBinDirectory 'Qt6Core.dll') -PathType Leaf)) { throw 'Installed Qt runtime is missing' }
if ($TimeoutSeconds -le 0) { throw 'TimeoutSeconds must be positive' }
$taskBinary = (Resolve-Path -LiteralPath $ProbeBinary).Path
$taskManifest = (Resolve-Path -LiteralPath $InputManifest).Path
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$taskOutput = (Resolve-Path -LiteralPath $OutputDirectory).Path
$taskStdout = Join-Path $taskOutput 'editing-raw.jsonl'
$taskStderr = Join-Path $taskOutput 'editing-stderr.log'
$taskOriginalPath = $env:PATH
$taskExit = $null
$taskTimedOut = $false
$taskStarted = [DateTime]::UtcNow
try {
    $env:PATH = "$QtBinDirectory;$taskOriginalPath"
    $taskProcess = Start-Process -FilePath $taskBinary -ArgumentList ('"' + $taskManifest + '"') -WorkingDirectory (Split-Path -Parent $taskBinary) -RedirectStandardOutput $taskStdout -RedirectStandardError $taskStderr -PassThru -WindowStyle Hidden
    # Retain the native handle before exit. Otherwise Windows can return null
    # ExitCode even when the Qt test failed; null must never become success.
    $taskNativeHandle = $taskProcess.Handle
    if (-not $taskProcess.WaitForExit($TimeoutSeconds * 1000)) {
        $taskTimedOut = $true
        $taskProcess.Kill()
        $taskProcess.WaitForExit()
    }
    $taskProcess.Refresh()
    $taskExit = $taskProcess.ExitCode
} finally {
    $env:PATH = $taskOriginalPath
    [ordered]@{
        schema = 'pando-m98-editing-run-v1'; startedUtc = $taskStarted.ToString('o'); finishedUtc = [DateTime]::UtcNow.ToString('o')
        nativePid = if ($taskProcess) { $taskProcess.Id } else { $null }
        probeBinary = $taskBinary; probeSha256 = (Get-FileHash -LiteralPath $taskBinary -Algorithm SHA256).Hash.ToLowerInvariant()
        manifest = $taskManifest; manifestSha256 = (Get-FileHash -LiteralPath $taskManifest -Algorithm SHA256).Hash.ToLowerInvariant()
        exitCode = $taskExit; timedOut = $taskTimedOut; stdout = $taskStdout; stderr = $taskStderr
        acceptedBy = $null; editingBudgets = $null; acceptanceStatus = 'BLOCKED: production place empty-v1; diagnostic execution is independent'
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskOutput 'run.json') -Encoding UTF8
}
if ($taskTimedOut) { throw 'Actual editing diagnostic timed out; raw evidence retained' }
if ($null -eq $taskExit) { throw 'Native ExitCode unavailable; raw evidence retained, success refused' }
if ($taskExit -ne 0) { throw "Actual editing diagnostic failed with native exit code $taskExit; raw evidence retained" }
$taskFinalLine = Get-Content -LiteralPath $taskStdout | Select-Object -Last 1
$taskReport = $taskFinalLine | ConvertFrom-Json
if ($taskReport.schema -ne 'pando-m98-editing-diagnostic-result-v1' -or $taskReport.caseCount -ne 14 -or $taskReport.failures -ne 0 -or $taskReport.status -ne 'DIAGNOSTIC') { throw 'Diagnostic result is incomplete; success refused' }
Write-Output "14 editing diagnostic cases completed. Full-data acceptance remains blocked. Evidence: $taskOutput"
