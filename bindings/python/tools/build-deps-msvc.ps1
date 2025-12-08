Param(
  [string]$Triplet = 'x64-windows',
  [string]$VcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
)
$ErrorActionPreference = 'Stop'

if (-not $VcpkgRoot -or $VcpkgRoot -eq '') {
  $VcpkgRoot = 'C:\vcpkg'
}
$env:VCPKG_INSTALLATION_ROOT = $VcpkgRoot

Write-Host "Building dependencies via vcpkg (triplet=$Triplet)..."
& vcpkg install "libarchive:$Triplet"

$prefix = Join-Path (Join-Path $VcpkgRoot 'installed') $Triplet
if (-not (Test-Path $prefix)) { throw "vcpkg prefix not found: $prefix" }

"LIBARCHIVE_ROOT=$prefix" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"CMAKE_PREFIX_PATH=$prefix" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"PATH=$prefix\bin;$($env:PATH)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"LIB=$prefix\lib;$($env:LIB)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"INCLUDE=$prefix\include;$($env:INCLUDE)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
