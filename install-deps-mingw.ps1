Param(
  [switch]$InContainer
)

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

# Expect MSYS2 pre-installed on windows-2022 runners (via msys2/setup-msys2 action).
# Install MinGW-UCRT toolchain and common deps.
$msysRoot = "C:\msys64"
$bashPath = Join-Path $msysRoot "usr\bin\bash.exe"
if ($InContainer) {
  Ensure-Chocolatey
  if (-not (Test-Path $bashPath)) {
    & choco install -y msys2 --params "/InstallDir:$msysRoot"
    if ($LastExitCode -ne 0) {
      throw "Chocolatey msys2 install failed with exit code ${LastExitCode}."
    }
  }
}
if (-not (Test-Path $bashPath)) {
  throw "MSYS2 bash not found at $bashPath. Ensure msys2/setup-msys2 action ran first."
}

function Invoke-MsysBash {
  param(
    [Parameter(Mandatory = $true)][string]$Command
  )
  & $bashPath -lc $Command
  if ($LastExitCode -ne 0) {
    throw "MSYS2 bash command failed with exit code ${LastExitCode}: $Command"
  }
}

function Invoke-Ucrt64Bash {
  param(
    [Parameter(Mandatory = $true)][string]$Command
  )
  $prefix = 'export MSYSTEM=UCRT64; export CHERE_INVOKING=1; export PATH=/ucrt64/bin:$PATH; '
  & $bashPath -lc ($prefix + $Command)
  if ($LastExitCode -ne 0) {
    throw "MSYS2 bash command failed with exit code ${LastExitCode}: $Command"
  }
}

Invoke-MsysBash "pacman -Syu --noconfirm --disable-download-timeout"
Invoke-MsysBash "pacman -S --noconfirm --disable-download-timeout git base-devel mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-libarchive mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-python-pip mingw-w64-ucrt-x86_64-python-setuptools mingw-w64-ucrt-x86_64-python-wheel mingw-w64-ucrt-x86_64-ruby mingw-w64-ucrt-x86_64-rust"

# Install/refresh Python tooling packages.
# MSYS2 Python is externally-managed (PEP 668); allow pip to modify it in CI/container images.
Invoke-Ucrt64Bash "python -m pip install --break-system-packages --upgrade --force-reinstall pip setuptools wheel build pybind11 pytest"

if ($InContainer) {
  Invoke-Ucrt64Bash "command -v cmake >/dev/null 2>&1 && cmake --version"
}
