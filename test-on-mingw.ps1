$ErrorActionPreference = 'Stop'
$repoRoot = if ($Env:GITHUB_WORKSPACE) { $Env:GITHUB_WORKSPACE } else { $PSScriptRoot }
$msysRoot = "C:\msys64"
$bashPath = Join-Path $msysRoot "usr\bin\bash.exe"
if (-not (Test-Path $bashPath)) { throw "MSYS2 bash not found at $bashPath" }

$env:ARCHIVE_R_REPO_WIN = $repoRoot
$env:ARCHIVE_R_TIMEOUT_WIN = Join-Path $repoRoot "run_with_timeout.py"

$repoPathMsys = (& $bashPath -lc 'cygpath -u "$ARCHIVE_R_REPO_WIN"').Trim()
Write-Host "DEBUG: repoPathMsys='$repoPathMsys'"
$timeoutPy = (& $bashPath -lc 'cygpath -u "$ARCHIVE_R_TIMEOUT_WIN"').Trim()
Write-Host "DEBUG: timeoutPy='$timeoutPy'"

# Create the bash script content
# Note: We escape $ for bash variables that shouldn't be expanded by PowerShell
$scriptContent = @"
#!/bin/bash
export MSYSTEM=UCRT64
export PATH=/ucrt64/bin:`$PATH
set -x

echo "[mingw] Starting test script"
echo "[mingw] Repo: $repoPathMsys"
echo "[mingw] Timeout Helper: $timeoutPy"

cd "$repoPathMsys" || exit 1

# Run Core Tests
echo "[mingw] Running core tests..."
# We run run_tests.sh directly. It should output to stdout/stderr which we capture.
python "$timeoutPy" 120 bash -c "chmod +x run_tests.sh && ./run_tests.sh"
RET=`$?
if [ `$RET -ne 0 ]; then
    echo "[mingw] Core tests failed with `$RET"
    exit `$RET
fi

# Build Ruby Binding
echo "[mingw] Building Ruby binding..."
python "$timeoutPy" 120 bash -c "./build.sh --package-ruby"
RET=`$?
if [ `$RET -ne 0 ]; then
    echo "[mingw] Ruby binding build failed with `$RET"
    exit `$RET
fi

# Build Python Binding
echo "[mingw] Building Python binding..."
python "$timeoutPy" 120 bash -c "./build.sh --package-python"
RET=`$?
if [ `$RET -ne 0 ]; then
    echo "[mingw] Python binding build failed with `$RET"
    exit `$RET
fi

echo "[mingw] All tasks completed successfully"
"@

$scriptPath = Join-Path $repoRoot "test_script.sh"
[IO.File]::WriteAllText($scriptPath, $scriptContent)

$scriptPathMsys = (& $bashPath -lc "cygpath -u '$scriptPath'").Trim()
Write-Host "DEBUG: Created bash script at $scriptPathMsys"

Write-Host "DEBUG: Running bash script..."

# Temporarily disable Stop on Error because bash writing to stderr (e.g. set -x) triggers NativeCommandError in PowerShell
$oldEAP = $ErrorActionPreference
$ErrorActionPreference = 'Continue'

# Execute the script, redirecting ALL output to mingw_exec.log
# We use a wrapper bash command to handle the redirection
# Ensure we are in the repo directory and use absolute path for log to avoid location issues
$logPathMsys = "$repoPathMsys/mingw_exec.log"
& $bashPath -lc "cd '$repoPathMsys' && bash '$scriptPathMsys' > '$logPathMsys' 2>&1"
$exitCode = $LASTEXITCODE

$ErrorActionPreference = $oldEAP

Write-Host "--- mingw_exec.log content ---"
if (Test-Path "mingw_exec.log") {
    Get-Content "mingw_exec.log"
} else {
    Write-Host "ERROR: mingw_exec.log not found"
}
Write-Host "--- end mingw_exec.log ---"

if ($exitCode -ne 0) {
    Write-Host "Bash exited with code $exitCode"
    exit $exitCode
}
