Param(
  [string]$PackageMode = "full"
)
$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
$msysRoot = "C:\msys64"
$bashPath = Join-Path $msysRoot "usr\bin\bash.exe"
if (-not (Test-Path $bashPath)) { throw "MSYS2 bash not found at $bashPath" }

$repoPathMsys = & $bashPath -lc "cygpath -u '$repoRoot'"
# Explicitly export the variable to ensure it reaches the bash session
$cmd = "export ARCHIVE_R_BUILD_NO_ISOLATION=1; cd $repoPathMsys && ./build.sh --rebuild-all"
switch ($PackageMode) {
  'core'   { }
  'python' { $cmd += ' --python-only --package-python' }
  'ruby'   { $cmd += ' --with-ruby --package-ruby' }
  default  { $cmd += ' --package-python --package-ruby' }
}

$env:MSYSTEM = "UCRT64"
& $bashPath -lc $cmd
if ($LastExitCode -ne 0) {
    throw "Build failed with exit code $LastExitCode"
}
