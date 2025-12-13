Param()
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Ensure-Chocolatey {
  if (Get-Command choco -ErrorAction SilentlyContinue) {
    return
  }
  Set-ExecutionPolicy Bypass -Scope Process -Force
  [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
  iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
}

Ensure-Chocolatey

# Tools used by build/test flows
choco install -y git cmake ninja python3 ruby

# MSVC toolchain (not always present in a base Windows container)
choco install -y visualstudio2022buildtools --execution-timeout 7200 --package-parameters "--passive --norestart"
choco install -y visualstudio2022-workload-vctools --execution-timeout 7200 --package-parameters "--includeRecommended"

# Ensure vcpkg exists
if (-not $env:VCPKG_INSTALLATION_ROOT) {
  $env:VCPKG_INSTALLATION_ROOT = 'C:\vcpkg'
}

$vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
if (-not (Test-Path $vcpkgRoot)) {
  git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
}

& "$vcpkgRoot\bootstrap-vcpkg.bat"
$env:PATH = "$vcpkgRoot;$env:PATH"

# Ensure install-deps-msvc.ps1 can write outputs even outside GitHub Actions
if (-not $env:GITHUB_ENV) {
  $env:GITHUB_ENV = 'C:\github_env.txt'
}

# Delegate vcpkg libarchive install + env export to canonical script
pwsh -File .\install-deps-msvc.ps1

# Persist the important variables into the image environment (Machine scope)
# install-deps-msvc.ps1 computes the canonical prefix path.
$prefix = Join-Path $env:VCPKG_INSTALLATION_ROOT 'installed' | Join-Path -ChildPath 'x64-windows-release'
if (Test-Path $prefix) {
  [Environment]::SetEnvironmentVariable('LIBARCHIVE_ROOT', $prefix, [EnvironmentVariableTarget]::Machine)
  [Environment]::SetEnvironmentVariable('CMAKE_PREFIX_PATH', $prefix, [EnvironmentVariableTarget]::Machine)
  $newPath = $env:PATH + ";$prefix\bin"
  [Environment]::SetEnvironmentVariable('PATH', $newPath, [EnvironmentVariableTarget]::Machine)
}
