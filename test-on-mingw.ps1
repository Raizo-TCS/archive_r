$ErrorActionPreference = 'Stop'
$repoRoot = if ($Env:GITHUB_WORKSPACE) { $Env:GITHUB_WORKSPACE } else { $PSScriptRoot }
$msysRoot = "C:/msys64"
$bashPath = "$msysRoot/usr/bin/bash.exe"
if (-not (Test-Path $bashPath)) { throw "MSYS2 bash not found at $bashPath" }

$env:ARCHIVE_R_REPO_WIN = $repoRoot
$env:ARCHIVE_R_TIMEOUT_WIN = Join-Path $repoRoot "run_with_timeout.py"

$repoPathMsys = (& $bashPath -lc 'cygpath -u "$ARCHIVE_R_REPO_WIN"').Trim()
$timeoutPy = (& $bashPath -lc 'cygpath -m "$ARCHIVE_R_TIMEOUT_WIN"').Trim()
# bashPath is already in Windows format with forward slashes, safe for Python
$bashPathForPy = $bashPath

# Create a temporary bash script to run tests
# This avoids complex quoting issues with passing a long command string to bash -c
$testScriptContent = @"
#!/bin/bash
set -euo pipefail
set -x

repo="$repoPathMsys"
timeout_py="$timeoutPy"
bash_exe="$bashPathForPy"

echo "[mingw] repo path: `$repo"
echo "[mingw] bash path: `$bash_exe"

if [ ! -d "`$repo" ]; then echo "[mingw] repo path not found" >&2; exit 1; fi
if [ ! -x "`$bash_exe" ]; then echo "[mingw] bash not found/executable: `$bash_exe" >&2; exit 1; fi

cd "`$repo"
pwd
ls -la

chmod +x run_tests.sh bindings/ruby/run_binding_tests.sh bindings/python/run_binding_tests.sh

if [ ! -f "`$timeout_py" ]; then echo "[mingw] timeout helper not found: `$timeout_py" >&2; exit 1; fi

python3 --version || echo "python3 not found"

echo "[mingw] Testing run_with_timeout wrapper..."
python3 -u "`$timeout_py" 10 "`$bash_exe" -lc "echo [mingw] wrapper test success" > wrapper_test.log 2>&1 || true
echo "Wrapper test log size:"
ls -l wrapper_test.log
cat wrapper_test.log

echo "[mingw] Running C++ tests (DIRECTLY)..."
# Run tests directly without python wrapper first to debug output issues
# We redirect to file to avoid pipe buffering issues, then cat it
./run_tests.sh > run_tests_wrapper.log 2>&1 || true
EXIT_CODE=\$?

echo "[mingw] run_tests.sh exit code: \$EXIT_CODE"
echo "run_tests_wrapper.log size:"
ls -l run_tests_wrapper.log

echo "--- C++ Test Output ---"
cat run_tests_wrapper.log
echo "-----------------------"

if [ \$EXIT_CODE -ne 0 ]; then
    echo "[mingw] C++ tests failed with exit code \$EXIT_CODE"
    exit \$EXIT_CODE
fi

# Check for failure in log even if exit code was 0 (shouldn't happen with set -e but good safety)
if grep -q "Test FAILED" run_tests_wrapper.log; then
    echo "[mingw] C++ tests failed (detected via log)"
    exit 1
fi

echo "[mingw] Running Ruby binding tests..."
python3 -u "`$timeout_py" 120 "`$bash_exe" -lc "cd \"`$repo\" && ./bindings/ruby/run_binding_tests.sh"

echo "[mingw] Running Python binding tests..."
python3 -u "`$timeout_py" 120 "`$bash_exe" -lc "cd \"`$repo\" && ./bindings/python/run_binding_tests.sh"
"@

$testScriptPath = Join-Path $repoRoot "run_mingw_tests_generated.sh"
# Convert to UTF-8 without BOM (PowerShell 5.1 default is UTF-16 or UTF-8 BOM)
[IO.File]::WriteAllText($testScriptPath, $testScriptContent)

# Convert Windows path to MSYS path for execution
$testScriptPathMsys = (& $bashPath -lc ('cygpath -u "{0}"' -f $testScriptPath)).Trim()

$env:MSYSTEM = "UCRT64"
& $bashPath -lc "bash '$testScriptPathMsys'"
if ($LastExitCode -ne 0) {
    throw "Tests failed with exit code $LastExitCode"
}
