Param()
$ErrorActionPreference = 'Stop'

# Install libarchive via vcpkg and expose environment variables for later steps.
if (-not $env:VCPKG_INSTALLATION_ROOT) {
  $env:VCPKG_INSTALLATION_ROOT = "C:\vcpkg"
}

Write-Host "Installing libarchive:x64-windows via vcpkg (Release only)..."
& vcpkg install libarchive:x64-windows-release --overlay-triplets=. --clean-after-build

$prefix = Join-Path $env:VCPKG_INSTALLATION_ROOT 'installed' | Join-Path -ChildPath 'x64-windows-release'
if (-not (Test-Path $prefix)) {
  throw "vcpkg prefix not found: $prefix"
}

"LIBARCHIVE_ROOT=$prefix" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"CMAKE_PREFIX_PATH=$prefix" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"PATH=$prefix\bin;$($env:PATH)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"LIB=$prefix\lib;$($env:LIB)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"INCLUDE=$prefix\include;$($env:INCLUDE)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
