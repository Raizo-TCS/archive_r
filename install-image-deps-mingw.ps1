Param(
  [string]$Pf = ""
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# Install Chocolatey
Set-ExecutionPolicy Bypass -Scope Process -Force
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# Install MSYS2 to C:\msys64 (required by build scripts)
choco install -y msys2 --params "/InstallDir:C:\msys64"

# Install dependencies using pacman
$bashPath = 'C:\msys64\usr\bin\bash.exe'
if (-not (Test-Path $bashPath)) {
  throw "MSYS2 bash not found at $bashPath"
}

& $bashPath -lc 'pacman -Syu --noconfirm'
& $bashPath -lc 'pacman -S --noconfirm git base-devel mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-libarchive mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-python-pip mingw-w64-ucrt-x86_64-python-setuptools mingw-w64-ucrt-x86_64-python-wheel mingw-w64-ucrt-x86_64-ruby mingw-w64-ucrt-x86_64-rust'

# Ensure pip is available and up-to-date inside UCRT64 environment
& $bashPath -lc 'export PATH=/ucrt64/bin:$PATH && python -m ensurepip --upgrade'

# Install Python tooling needed by packaging/test flows
& $bashPath -lc 'export PATH=/ucrt64/bin:$PATH && python -m pip install --upgrade --force-reinstall pip setuptools wheel build pybind11 pytest twine'
