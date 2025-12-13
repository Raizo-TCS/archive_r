# escape=`

# Use Windows Server Core 2022 as the base image
FROM mcr.microsoft.com/windows/servercore:ltsc2022

# Set shell to PowerShell
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

COPY install-deps-mingw.ps1 C:\io\install-deps-mingw.ps1
COPY install-deps-mingw-container.ps1 C:\io\install-deps-mingw-container.ps1
RUN & C:\io\install-deps-mingw-container.ps1; Remove-Item -Force C:\io\install-deps-mingw.ps1, C:\io\install-deps-mingw-container.ps1

# Define working directory
WORKDIR C:\io

# Default command
CMD ["powershell"]
