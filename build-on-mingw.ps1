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
# Also skip twine check to avoid building rust dependencies (nh3) from source
$cmd = "export MSYSTEM=UCRT64; export PATH=/usr/bin:/ucrt64/bin:`$PATH; export ARCHIVE_R_BUILD_NO_ISOLATION=1; export ARCHIVE_R_SKIP_TWINE_CHECK=1; cd $repoPathMsys && ./build.sh --rebuild-all"
switch ($PackageMode) {
  'core'   { }
  'python' { $cmd += ' --python-only --package-python' }
  'ruby'   { $cmd += ' --with-ruby --package-ruby' }
  default  { $cmd += ' --package-python --package-ruby' }
}
& $bashPath -lc $cmd
if ($LastExitCode -ne 0) {
    throw "Build failed with exit code $LastExitCode"
}
