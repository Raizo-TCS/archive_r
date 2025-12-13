$ErrorActionPreference = 'Stop'

# Expect MSYS2 pre-installed on windows-2022 runners (via msys2/setup-msys2 action).
# Install MinGW-UCRT toolchain and common deps.
$msysRoot = "C:\msys64"
$bashPath = Join-Path $msysRoot "usr\bin\bash.exe"
if (-not (Test-Path $bashPath)) {
  throw "MSYS2 bash not found at $bashPath. Ensure msys2/setup-msys2 action ran first."
}

function Invoke-MsysBash {
  param(
    [Parameter(Mandatory = $true)][string]$Command
  )
  & $bashPath -lc $Command
  if ($LastExitCode -ne 0) {
    throw "MSYS2 bash command failed with exit code $LastExitCode: $Command"
  }
}

Invoke-MsysBash "pacman -Syu --noconfirm"
Invoke-MsysBash "pacman -S --noconfirm git base-devel mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-libarchive mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-python-pip mingw-w64-ucrt-x86_64-python-setuptools mingw-w64-ucrt-x86_64-python-wheel mingw-w64-ucrt-x86_64-ruby mingw-w64-ucrt-x86_64-rust"

# Reinstall pip stack to ensure bundled vendored modules (distlib, etc.) are present.
Invoke-MsysBash "python3 -m ensurepip --upgrade"
Invoke-MsysBash "python3 -m pip install --upgrade --force-reinstall pip setuptools wheel build pybind11 pytest"
