# escape=`

# Use Windows Server Core 2022 as the base image
FROM mcr.microsoft.com/windows/servercore:ltsc2022@sha256:d4c6d1a8a1a306b12691c3b2e5e3a8bfad786cbd6b7831cd74a9a6a99eab08ad

# Set shell to PowerShell
SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

COPY install-deps-mingw.ps1 C:\io\install-deps-mingw.ps1
RUN C:\io\install-deps-mingw.ps1 -InContainer; Remove-Item -Force C:\io\install-deps-mingw.ps1

# Define working directory
WORKDIR C:\io

# Default command
CMD ["powershell"]
