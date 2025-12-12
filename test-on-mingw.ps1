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

# Use 'python' instead of 'python3' as it is more standard in MinGW UCRT64
# Also ensure we capture stdout/stderr of the wrapper script itself
$runTestsCmd = "cd `"$repoPathMsys`" && echo 'DEBUG: Wrapper starting' && chmod +x run_tests.sh && ./run_tests.sh > run_tests.log 2>&1; RES=$?; echo 'DEBUG: Wrapper finished with '$RES; echo '--- run_tests.log content ---'; cat run_tests.log; echo '--- end log ---'; exit $RES"
$rubyBindingCmd = "cd `"$repoPathMsys`" && ./build.sh --package-ruby"
$pythonBindingCmd = "cd `"$repoPathMsys`" && ./build.sh --package-python"

$cmdLines = @(
        'export MSYSTEM=UCRT64'
        'export PATH=/ucrt64/bin:$PATH'
        'set -x'
        'repo="{0}"' -f $repoPathMsys
        'timeout_py="{0}"' -f $timeoutPy
        'bash_exe="{0}"' -f $bashPath
        'echo "[mingw] repo path: $repo"'
        'echo "[mingw] bash path: $bash_exe"'
        'if [ ! -d "$repo" ]; then echo "[mingw] repo path not found" >&2; exit 1; fi'
        'if [ ! -x "$bash_exe" ]; then echo "[mingw] bash not found/executable: $bash_exe" >&2; exit 1; fi'
        'cd "$repo"'
        'pwd'
        'if [ ! -f "$timeout_py" ]; then echo "[mingw] timeout helper not found: $timeout_py" >&2; exit 1; fi'
        ('python "{0}" 120 "{1}" -lc "{2}"' -f $timeoutPy, $bashPath, $runTestsCmd)
        ('python "{0}" 120 "{1}" -lc "{2}"' -f $timeoutPy, $bashPath, $rubyBindingCmd)
        ('python "{0}" 120 "{1}" -lc "{2}"' -f $timeoutPy, $bashPath, $pythonBindingCmd)
)

$cmd = $cmdLines -join ' && '

Write-Host "DEBUG: Running bash command..."
# Capture all output to a file to avoid console buffering issues
# Temporarily disable Stop on Error because bash writing to stderr (e.g. set -x) triggers NativeCommandError
$oldEAP = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $bashPath -lc $cmd > mingw_exec.log 2>&1
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
