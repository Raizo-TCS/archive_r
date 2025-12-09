$ErrorActionPreference = 'Stop'
$repoRoot = if ($Env:GITHUB_WORKSPACE) { $Env:GITHUB_WORKSPACE } else { Split-Path -Parent $PSScriptRoot }
$msysRoot = "C:\msys64"
$bashPath = Join-Path $msysRoot "usr\bin\bash.exe"
if (-not (Test-Path $bashPath)) { throw "MSYS2 bash not found at $bashPath" }

$repoPathMsys = (& $bashPath -lc "cygpath -u \"$repoRoot\"").Trim()
$cmd = "cd '$repoPathMsys' && python3 ./run_with_timeout.py 120 ./run_tests.sh && python3 ./run_with_timeout.py 120 ./bindings/ruby/run_binding_tests.sh && python3 ./run_with_timeout.py 120 ./bindings/python/run_binding_tests.sh"
& $bashPath -lc $cmd
