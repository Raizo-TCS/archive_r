# escape=`

# Use Windows Server Core 2022 as the base image
FROM mcr.microsoft.com/windows/servercore:ltsc2022@sha256:d31ac6a9b0c435679f941677661dd2fc555620348198c506e78e9ba70352e406

# Set shell to PowerShell
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

COPY install-deps-mingw.ps1 C:\io\install-deps-mingw.ps1
RUN C:\io\install-deps-mingw.ps1 -InContainer; Remove-Item -Force C:\io\install-deps-mingw.ps1

# Define working directory
WORKDIR C:\io

# Default command
CMD ["powershell"]
