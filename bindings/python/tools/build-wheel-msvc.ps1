Param(
  [Parameter(Mandatory=$true)][string]$PythonVersion,
  [Parameter(Mandatory=$true)][ValidateSet('x86_64','arm64')][string]$Arch = 'x86_64'
)
$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
Push-Location $repoRoot
try {
  python -m pip install --upgrade pip setuptools wheel pybind11 build delvewheel

  # Ensure licensing materials are present inside bindings/python before building.
  $pyRoot = Join-Path $repoRoot 'bindings\python'
  $srcLicenses = Join-Path $repoRoot 'LICENSES'
  $dstLicenses = Join-Path $pyRoot 'LICENSES'
  if (Test-Path $dstLicenses) { Remove-Item -Recurse -Force $dstLicenses }
  New-Item -ItemType Directory -Force -Path $dstLicenses | Out-Null
  if (Test-Path $srcLicenses) {
    Copy-Item -Recurse -Force (Join-Path $srcLicenses '*') $dstLicenses
  }
  $rootLicense = Join-Path $repoRoot 'LICENSE'
  if (Test-Path $rootLicense) { Copy-Item -Force $rootLicense (Join-Path $pyRoot 'LICENSE') }
  $rootNotice = Join-Path $repoRoot 'NOTICE'
  if (Test-Path $rootNotice) { Copy-Item -Force $rootNotice (Join-Path $pyRoot 'NOTICE') }

  $platTag = if ($Arch -eq 'arm64') { 'win_arm64' } else { 'win_amd64' }
  $targetDir = "bindings/python/dist/${platTag}-${PythonVersion}"
  $tempDir = Join-Path $targetDir "unrepaired"
  New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

  python -m pip wheel bindings/python -w $tempDir --no-deps --config-settings "--build-option=--plat-name=${platTag}"

  $wheelFile = (Get-ChildItem "$tempDir\*.whl" | Select-Object -First 1).FullName
  
  $dllArgs = @()
  if ($env:LIBARCHIVE_ROOT) {
      $dllPath = Join-Path $env:LIBARCHIVE_ROOT "bin"
      if (Test-Path $dllPath) {
          $dllArgs += "--add-path"
          $dllArgs += $dllPath
      }
  }

  Write-Host "Repairing wheel with delvewheel..."
  delvewheel repair $dllArgs -w $targetDir $wheelFile

  Remove-Item -Recurse -Force $tempDir

  # Smoke test
  python -m venv .venv
  .\.venv\Scripts\Activate.ps1
  pip install --no-index (Get-ChildItem "$targetDir\*.whl" | Select-Object -First 1).FullName
  python -c "import archive_r; print('${platTag} validated ' + archive_r.__version__)"
} finally {
  Pop-Location
}
