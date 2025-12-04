param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("pf5", "pf6")]
    [string]$Platform
)

$ErrorActionPreference = "Stop"

function Convert-ToPosixPath {
    param([string]$Path)
    if (-not $Path) {
        throw "Workspace path is empty"
    }
    $trimmed = $Path.TrimEnd('\\')
    if ($trimmed.Length -lt 3 -or $trimmed[1] -ne ':') {
        throw "Unsupported path format: $Path"
    }
    $drive = $trimmed.Substring(0, 1).ToLower()
    $rest = $trimmed.Substring(2).Replace('\\', '/')
    return "/$drive/$rest"
}

$workspace = $env:ARCHIVE_R_WORKSPACE
if (-not $workspace) {
    $workspace = "C:\\workspace"
}
if (-not (Test-Path -LiteralPath $workspace)) {
    throw "Workspace not found at $workspace"
}

# Install dependencies (Python, Ruby, Libarchive)
function Install-Dependencies {
    param([string]$Platform)
    Write-Host "Installing dependencies for $Platform..."
    
    # Install Python and Ruby via Chocolatey (latest versions)
    choco install python ruby -y --no-progress
    
    # Refresh environment variables
    refreshenv

    if ($Platform -eq "pf5") {
        # MSVC: Install libarchive via vcpkg
        Write-Host "Bootstrapping vcpkg..."
        Set-Location "C:\vcpkg"
        .\bootstrap-vcpkg.bat -disableMetrics
        .\vcpkg.exe integrate install
        
        Write-Host "Installing libarchive:x64-windows..."
        .\vcpkg.exe install libarchive:x64-windows
        
        $env:LIBARCHIVE_ROOT = "C:\vcpkg\installed\x64-windows"
        $env:PATH = "$env:LIBARCHIVE_ROOT\bin;" + $env:PATH
    }
    elseif ($Platform -eq "pf6") {
        # MinGW: Install libarchive via pacman
        Write-Host "Installing mingw-w64-ucrt-x86_64-libarchive..."
        $bashExe = "C:\\tools\\msys64\\usr\\bin\\bash.exe"
        & $bashExe -lc "pacman -S --noconfirm --needed mingw-w64-ucrt-x86_64-libarchive"
        
        $env:LIBARCHIVE_ROOT = "C:\tools\msys64\ucrt64"
    }
}

Install-Dependencies -Platform $Platform

$posixWorkspace = Convert-ToPosixPath -Path $workspace
$buildCommand = "./build.sh --rebuild-all --package-python --package-ruby"
$testCommand = "python3 ./.github/scripts/ci/run_with_timeout.py 120 ./run_tests.sh"
$scriptBody = "set -euo pipefail; cd $posixWorkspace; $buildCommand; $testCommand"

switch ($Platform) {
    "pf5" {
        $bashExe = "C:\\Program Files\\Git\\bin\\bash.exe"
        if (-not (Test-Path -LiteralPath $bashExe)) {
            throw "bash.exe not found at $bashExe"
        }
        & $bashExe -lc $scriptBody
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed with exit code $LASTEXITCODE"
        }
    }
    "pf6" {
        $bashExe = "C:\\tools\\msys64\\usr\\bin\\bash.exe"
        if (-not (Test-Path -LiteralPath $bashExe)) {
            throw "MSYS2 bash.exe not found at $bashExe"
        }
        $env:MSYSTEM = "UCRT64"
        $env:CHERE_INVOKING = "1"
        # Add UCRT64 bin path to ensure gcc/make/cmake are found
        $env:PATH = "C:\tools\msys64\ucrt64\bin;C:\tools\msys64\usr\bin;" + $env:PATH
        # Force Unix Makefiles generator (MSYS2 make) to avoid NMake default
        $env:CMAKE_GENERATOR = "Unix Makefiles"
        & $bashExe -lc $scriptBody
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed with exit code $LASTEXITCODE"
        }
    }
    default {
        throw "Unsupported platform $Platform"
    }
}
