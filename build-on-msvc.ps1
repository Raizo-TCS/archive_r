Param(
  [string]$PackageMode = "full"
)
$ErrorActionPreference = 'Stop'

Push-Location $PSScriptRoot
try {

# Use Git Bash to run the existing build.sh to avoid WSL.
function Get-GitBashPath {
  $candidates = @(
    (Join-Path $Env:ProgramFiles 'Git\bin\bash.exe'),
    (Join-Path $Env:ProgramFiles 'Git\usr\bin\bash.exe'),
    (Join-Path ${Env:ProgramW6432} 'Git\bin\bash.exe'),
    (Join-Path ${Env:ProgramFiles(x86)} 'Git\bin\bash.exe')
  )
  foreach ($p in $candidates) {
    if ($p -and (Test-Path $p)) { return $p }
  }
  $cmd = Get-Command bash.exe -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  throw "Git Bash not found. Install Git for Windows."
}

$bash = Get-GitBashPath
$buildArgs = @('--rebuild-all')

switch ($PackageMode) {
  'core'   { }
  'python' { $buildArgs += '--python-only' ; $buildArgs += '--package-python' }
  'ruby'   { $buildArgs += '--with-ruby' ; $buildArgs += '--package-ruby' }
  default  { $buildArgs += '--package-python' ; $buildArgs += '--package-ruby' }
}

# Force CMake to use the Visual Studio generator instead of defaulting to MinGW Makefiles
# when running inside Git Bash.
$Env:CMAKE_GENERATOR = "Visual Studio 17 2022"
$Env:CMAKE_GENERATOR_PLATFORM = "x64"

# Add build output directory to PATH so that Python tests can find archive_r_core.dll
$Env:PATH = "$PSScriptRoot\build\Release;$Env:PATH"

  & $bash ./build.sh @buildArgs
  if ($LastExitCode -ne 0) { throw "Build failed with exit code $LastExitCode" }
} finally {
  Pop-Location
}
