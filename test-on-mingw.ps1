$ErrorActionPreference = 'Stop'
$repoRoot = if ($Env:GITHUB_WORKSPACE) { $Env:GITHUB_WORKSPACE } else { $PSScriptRoot }
$msysRoot = "C:\msys64"
$bashPath = Join-Path $msysRoot "usr\bin\bash.exe"
if (-not (Test-Path $bashPath)) { throw "MSYS2 bash not found at $bashPath" }

$env:ARCHIVE_R_REPO_WIN = $repoRoot
$env:ARCHIVE_R_TIMEOUT_WIN = Join-Path $repoRoot "run_with_timeout.py"

$repoPathMsys = (& $bashPath -lc 'cygpath -u "$ARCHIVE_R_REPO_WIN"').Trim()
$timeoutPy = (& $bashPath -lc 'cygpath -u "$ARCHIVE_R_TIMEOUT_WIN"').Trim()
$runTestsCmd = "cd `"$repoPathMsys`" && ./run_tests.sh"
$rubyBindingCmd = "cd `"$repoPathMsys`" && ./bindings/ruby/run_binding_tests.sh"
$pythonBindingCmd = "cd `"$repoPathMsys`" && ./bindings/python/run_binding_tests.sh"

$cmdLines = @(
        'set -euo pipefail'
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
        ('python3 -u "{0}" 120 "{1}" -lc "{2}"' -f $timeoutPy, $bashPath, $runTestsCmd)
        ('python3 -u "{0}" 120 "{1}" -lc "{2}"' -f $timeoutPy, $bashPath, $rubyBindingCmd)
        ('python3 -u "{0}" 120 "{1}" -lc "{2}"' -f $timeoutPy, $bashPath, $pythonBindingCmd)
)

$cmd = $cmdLines -join ' && '

& $bashPath -lc $cmd
