Param(
  [string]$PackageMode = "full"
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$msysRoot = "C:\msys64"
$bashPath = Join-Path $msysRoot "usr\bin\bash.exe"
if (-not (Test-Path $bashPath)) { throw "MSYS2 bash not found at $bashPath" }

$repoPathMsys = & $bashPath -lc "cygpath -u '$repoRoot'"
$cmd = "cd $repoPathMsys && ./build.sh --rebuild-all"
switch ($PackageMode) {
  'core'   { }
  'python' { $cmd += ' --python-only --package-python' }
  'ruby'   { $cmd += ' --with-ruby --package-ruby' }
  default  { $cmd += ' --package-python --package-ruby' }
}

& $bashPath -lc $cmd
