# escape=`

# Use Windows Server Core 2022 as the base image (matches GitHub Actions windows-2022 runner)
FROM mcr.microsoft.com/windows/servercore:ltsc2022

# Set shell to PowerShell
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

# Install Chocolatey
RUN Set-ExecutionPolicy Bypass -Scope Process -Force; `
    [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; `
    iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# Install build tools and languages
# - git, cmake, ninja: Build tools
# - python3, ruby: For bindings and tests
# - visualstudio2022buildtools: C++ compiler (MSVC)
RUN choco install -y git cmake ninja python3 ruby; `
    choco install -y visualstudio2022buildtools --package-parameters "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --norestart"

# Set up environment variables for tools
ENV VCPKG_ROOT=C:\vcpkg
ENV VCPKG_DEFAULT_TRIPLET=x64-windows-release

# Install vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git $env:VCPKG_ROOT; `
    & "$env:VCPKG_ROOT\bootstrap-vcpkg.bat"

# Install libarchive and dependencies via vcpkg
# This step pre-compiles all dependencies into the image
RUN & "$env:VCPKG_ROOT\vcpkg.exe" install libarchive:x64-windows-release --clean-after-build

# Set up environment variables for usage in CI
ENV LIBARCHIVE_ROOT="C:\vcpkg\installed\x64-windows-release"
ENV CMAKE_PREFIX_PATH="C:\vcpkg\installed\x64-windows-release"
# Add vcpkg installed bin/lib/include to paths
RUN $newPath = $env:PATH + ';C:\vcpkg\installed\x64-windows-release\bin'; `
    [Environment]::SetEnvironmentVariable('PATH', $newPath, [EnvironmentVariableTarget]::Machine)

# Define working directory
WORKDIR C:\io

# Default command
CMD ["powershell"]
