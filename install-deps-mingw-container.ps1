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

# Ensure MSYS2 exists at C:\msys64 (expected by install-deps-mingw.ps1)
$msysRoot = 'C:\msys64'
$bashPath = Join-Path $msysRoot 'usr\bin\bash.exe'
if (-not (Test-Path $bashPath)) {
  choco install -y msys2 --params "/InstallDir:$msysRoot"
}

# Delegate the rest to the canonical dependency script
powershell -ExecutionPolicy Bypass -File .\install-deps-mingw.ps1

# Verify key tools exist to avoid publishing a broken image.
& $bashPath -lc "command -v cmake >/dev/null 2>&1 && cmake --version"
if ($LastExitCode -ne 0) {
  throw "CMake was not found after dependency installation."
}
