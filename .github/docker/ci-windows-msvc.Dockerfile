# escape=`

# Use Windows Server Core 2022 as the base image (matches GitHub Actions windows-2022 runner)
FROM mcr.microsoft.com/windows/servercore:ltsc2022@sha256:3750d7fcd320130cc2ce61954902b71729e85ec2c07c5a2e83a6d6c7f34a61e5

# Set shell to PowerShell
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

COPY install-deps-msvc.ps1 C:\io\install-deps-msvc.ps1
RUN C:\io\install-deps-msvc.ps1 -InContainer; Remove-Item -Force C:\io\install-deps-msvc.ps1

# Define working directory
WORKDIR C:\io

# Default command
CMD ["powershell"]
