Param(
  [string]$Pf = ""
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# Install Chocolatey
Set-ExecutionPolicy Bypass -Scope Process -Force
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# Build tools and languages
choco install -y git cmake ninja python3 ruby

# C++ compiler (MSVC)
choco install -y visualstudio2022buildtools --execution-timeout 7200 --package-parameters "--passive --norestart"
choco install -y visualstudio2022-workload-vctools --execution-timeout 7200 --package-parameters "--includeRecommended"

# Install vcpkg
$VcpkgRoot = 'C:\vcpkg'
$Triplet = 'x64-windows-release'

[Environment]::SetEnvironmentVariable('VCPKG_ROOT', $VcpkgRoot, [EnvironmentVariableTarget]::Machine)
[Environment]::SetEnvironmentVariable('VCPKG_DEFAULT_TRIPLET', $Triplet, [EnvironmentVariableTarget]::Machine)

if (-not (Test-Path $VcpkgRoot)) {
  git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
}

& "$VcpkgRoot\bootstrap-vcpkg.bat"
& "$VcpkgRoot\vcpkg.exe" install libarchive:$Triplet --clean-after-build

$prefix = "$VcpkgRoot\installed\$Triplet"
[Environment]::SetEnvironmentVariable('LIBARCHIVE_ROOT', $prefix, [EnvironmentVariableTarget]::Machine)
[Environment]::SetEnvironmentVariable('CMAKE_PREFIX_PATH', $prefix, [EnvironmentVariableTarget]::Machine)

# Add vcpkg installed bin to PATH
$newPath = $env:PATH + ";$prefix\bin"
[Environment]::SetEnvironmentVariable('PATH', $newPath, [EnvironmentVariableTarget]::Machine)

# Python tooling used by packaging/test flows
python -m pip install --upgrade pip setuptools wheel pybind11 build pytest twine
