Param(
  [Parameter(Mandatory=$true)][string]$PythonVersion,
  [Parameter(Mandatory=$true)][ValidateSet('x86_64','arm64')][string]$Arch = 'x86_64'
)
$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Push-Location $repoRoot
try {
  python -m pip install --upgrade pip setuptools wheel pybind11 build delocate

  $platTag = if ($Arch -eq 'arm64') { 'win_arm64' } else { 'win_amd64' }
  $targetDir = "bindings/python/dist/${platTag}-${PythonVersion}"
  New-Item -ItemType Directory -Force -Path $targetDir | Out-Null

  python -m pip wheel bindings/python -w $targetDir --no-deps --config-settings "--build-option=--plat-name=${platTag}"

  # Smoke test
  python -m venv .venv
  .\.venv\Scripts\Activate.ps1
  pip install --no-index (Get-ChildItem "$targetDir\*.whl" | Select-Object -First 1).FullName
  python -c "import archive_r; print('${platTag} validated ' + archive_r.__version__)"
} finally {
  Pop-Location
}
