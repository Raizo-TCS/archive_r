$ErrorActionPreference = 'Stop'
$repoRoot = if ($Env:GITHUB_WORKSPACE) { $Env:GITHUB_WORKSPACE } else { $PSScriptRoot }
$msysRoot = "C:\msys64"
$bashPath = Join-Path $msysRoot "usr\bin\bash.exe"
if (-not (Test-Path $bashPath)) { throw "MSYS2 bash not found at $bashPath" }

$env:ARCHIVE_R_REPO_WIN = $repoRoot
$env:ARCHIVE_R_TIMEOUT_WIN = Join-Path $repoRoot "run_with_timeout.py"

$repoPathMsys = (& $bashPath -lc 'cygpath -u "$ARCHIVE_R_REPO_WIN"').Trim()
$timeoutPy = (& $bashPath -lc 'cygpath -m "$ARCHIVE_R_TIMEOUT_WIN"').Trim()
$bashPathForPy = (& $bashPath -lc ('cygpath -m "{0}"' -f $bashPath)).Trim()

$runTestsCmd = "cd `"$repoPathMsys`" && ./run_tests.sh"
$rubyBindingCmd = "cd `"$repoPathMsys`" && ./bindings/ruby/run_binding_tests.sh"
$pythonBindingCmd = "cd `"$repoPathMsys`" && ./bindings/python/run_binding_tests.sh"

$cmdLines = @(
	'set -euo pipefail'
	'set -x'
	'repo="{0}"' -f $repoPathMsys
	'timeout_py="{0}"' -f $timeoutPy
	'bash_exe="{0}"' -f $bashPathForPy
	'echo "[mingw] repo path: $repo"'
	'echo "[mingw] bash path: $bash_exe"'
	'if [ ! -d "$repo" ]; then echo "[mingw] repo path not found" >&2; exit 1; fi'
	'if [ ! -x "$bash_exe" ]; then echo "[mingw] bash not found/executable: $bash_exe" >&2; exit 1; fi'
	'cd "$repo"'
	'pwd'
	'ls -la'
	'chmod +x run_tests.sh bindings/ruby/run_binding_tests.sh bindings/python/run_binding_tests.sh'
	'if [ ! -f "$timeout_py" ]; then echo "[mingw] timeout helper not found: $timeout_py" >&2; exit 1; fi'
	'python3 --version || echo "python3 not found"'
	('python3 -u "{0}" 120 "{1}" -lc "{2}"' -f $timeoutPy, $bashPathForPy, $runTestsCmd)
	('python3 -u "{0}" 120 "{1}" -lc "{2}"' -f $timeoutPy, $bashPathForPy, $rubyBindingCmd)
	('python3 -u "{0}" 120 "{1}" -lc "{2}"' -f $timeoutPy, $bashPathForPy, $pythonBindingCmd)
)

$cmd = $cmdLines -join ' && '

$env:MSYSTEM = "UCRT64"
& $bashPath -lc $cmd
if ($LastExitCode -ne 0) {
    throw "Tests failed with exit code $LastExitCode"
}
