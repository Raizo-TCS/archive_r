# escape=`

# Use Windows Server Core 2022 as the base image (matches GitHub Actions windows-2022 runner)
FROM mcr.microsoft.com/windows/servercore:ltsc2022@sha256:e000e9a1712065a0218447c20ae19984b447fa741d11cf64696b8a1172fcd7da

# Set shell to PowerShell
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

COPY install-deps-msvc.ps1 C:\io\install-deps-msvc.ps1
RUN C:\io\install-deps-msvc.ps1 -InContainer; Remove-Item -Force C:\io\install-deps-msvc.ps1

# Define working directory
WORKDIR C:\io

# Default command
CMD ["powershell"]
