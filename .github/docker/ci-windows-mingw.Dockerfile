# escape=`

# Use Windows Server Core 2022 as the base image
FROM mcr.microsoft.com/windows/servercore:ltsc2022

# Set shell to PowerShell
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

# Install Chocolatey
RUN Set-ExecutionPolicy Bypass -Scope Process -Force; `
    [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; `
    iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# Install MSYS2 to C:\msys64 (required by build scripts)
RUN choco install -y msys2 --params "/InstallDir:C:\msys64"

# Install dependencies using pacman
# We use bash -lc to run in the MSYS2 environment
# Note: We must explicitly add /ucrt64/bin to PATH to find the installed python/ruby/etc.
RUN $bashPath = 'C:\msys64\usr\bin\bash.exe'; `
    & $bashPath -lc 'pacman -Syu --noconfirm'; `
    & $bashPath -lc 'pacman -S --noconfirm git base-devel mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-libarchive mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-python-pip mingw-w64-ucrt-x86_64-python-setuptools mingw-w64-ucrt-x86_64-python-wheel mingw-w64-ucrt-x86_64-ruby mingw-w64-ucrt-x86_64-rust'; `
    & $bashPath -lc 'export PATH=/ucrt64/bin:$PATH && python -m ensurepip --upgrade'; `
    & $bashPath -lc 'export PATH=/ucrt64/bin:$PATH && python -m pip install --upgrade --force-reinstall pip setuptools wheel build pybind11 pytest'

# Define working directory
WORKDIR C:\io

# Default command
CMD ["powershell"]
