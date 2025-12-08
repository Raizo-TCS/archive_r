Param(
  [string]$PackageMode = "full"
)
$ErrorActionPreference = 'Stop'

Push-Location $PSScriptRoot
try {

# Use Git Bash to run the existing build.sh to avoid WSL.
$bash = (Get-Command bash.exe -ErrorAction Stop).Source
$buildArgs = @('--rebuild-all')

switch ($PackageMode) {
  'core'   { }
  'python' { $buildArgs += '--python-only' ; $buildArgs += '--package-python' }
  'ruby'   { $buildArgs += '--with-ruby' ; $buildArgs += '--package-ruby' }
  default  { $buildArgs += '--package-python' ; $buildArgs += '--package-ruby' }
}

  & $bash ./build.sh @buildArgs
} finally {
  Pop-Location
}
