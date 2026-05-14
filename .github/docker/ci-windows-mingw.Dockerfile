# escape=`

# Use Windows Server Core 2022 as the base image
FROM mcr.microsoft.com/windows/servercore:ltsc2022@sha256:86da395cfd2b35dbfc2e9d08719550c51b0570c394bff8f92622a19234766185

# Set shell to PowerShell
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

COPY install-deps-mingw.ps1 C:\io\install-deps-mingw.ps1
RUN C:\io\install-deps-mingw.ps1 -InContainer; Remove-Item -Force C:\io\install-deps-mingw.ps1

# Define working directory
WORKDIR C:\io

# Default command
CMD ["powershell"]
