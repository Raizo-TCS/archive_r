$ErrorActionPreference = 'Stop'
$repoRoot = if ($Env:GITHUB_WORKSPACE) { $Env:GITHUB_WORKSPACE } else { Split-Path -Parent $PSScriptRoot }
$msysRoot = "C:\msys64"
$bashPath = Join-Path $msysRoot "usr\bin\bash.exe"
if (-not (Test-Path $bashPath)) { throw "MSYS2 bash not found at $bashPath" }

$env:ARCHIVE_R_REPO_WIN = $repoRoot
$env:ARCHIVE_R_TIMEOUT_WIN = Join-Path $repoRoot "run_with_timeout.py"

$repoPathMsys = (& $bashPath -lc 'cygpath -u "$ARCHIVE_R_REPO_WIN"').Trim()
$timeoutPy = (& $bashPath -lc 'cygpath -u "$ARCHIVE_R_TIMEOUT_WIN"').Trim()

$cmd = @(
	'set -euo pipefail'
	"repo=`"$repoPathMsys`""
	"timeout_py=`"$timeoutPy`""
	'echo "[mingw] repo path: $repo"'
	'if [ ! -d "$repo" ]; then echo "[mingw] repo path not found" >&2; exit 1; fi'
	'cd "$repo"'
	'pwd'
	'if [ ! -f "$timeout_py" ]; then echo "[mingw] timeout helper not found: $timeout_py" >&2; exit 1; fi'
	'python3 "$timeout_py" 120 ./run_tests.sh'
	'python3 "$timeout_py" 120 ./bindings/ruby/run_binding_tests.sh'
	'python3 "$timeout_py" 120 ./bindings/python/run_binding_tests.sh'
) -join ' && '

& $bashPath -lc $cmd
