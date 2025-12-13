Param(
  [switch]$InContainer
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Invoke-Native {
  param(
    [Parameter(Mandatory = $true)][string]$FilePath,
    [Parameter(Mandatory = $false)][string[]]$Arguments = @()
  )
  & $FilePath @Arguments
  if ($LastExitCode -ne 0) {
    $argText = ($Arguments -join ' ')
    throw "Command failed with exit code ${LastExitCode}: $FilePath $argText"
  }
}

function Write-LogTail {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [int]$Lines = 200
  )
  if (-not (Test-Path $Path)) {
    return
  }
  Write-Host "----- BEGIN LOG: $Path (tail $Lines) -----"
  try {
    Get-Content -Path $Path -Tail $Lines -ErrorAction Stop
  } catch {
    Write-Warning "Failed to read log: $Path ($($_.Exception.Message))"
  }
  Write-Host "----- END LOG: $Path -----"
}

function Dump-InstallerLogs {
  Write-Host '=== Diagnostic logs (Chocolatey / VS Installer) ==='
  Write-LogTail -Path 'C:\ProgramData\chocolatey\logs\chocolatey.log' -Lines 200

  $candidateDirs = @(
    'C:\Users\ContainerAdministrator\AppData\Local\Temp\chocolatey',
    'C:\Users\ContainerAdministrator\AppData\Local\Temp',
    'C:\Windows\Temp'
  )

  foreach ($dir in $candidateDirs) {
    if (-not (Test-Path $dir)) { continue }

    # Visual Studio installer logs often appear as dd_installer_*.log
    Get-ChildItem -Path $dir -Filter 'dd_installer_*.log' -File -ErrorAction SilentlyContinue |
      Sort-Object LastWriteTime -Descending |
      Select-Object -First 3 |
      ForEach-Object { Write-LogTail -Path $_.FullName -Lines 200 }

    # Chocolatey package install scripts may emit .log files under temp
    Get-ChildItem -Path $dir -Filter '*.log' -File -ErrorAction SilentlyContinue |
      Sort-Object LastWriteTime -Descending |
      Select-Object -First 2 |
      ForEach-Object { Write-LogTail -Path $_.FullName -Lines 120 }
  }
}

function Ensure-Chocolatey {
  if (Get-Command choco -ErrorAction SilentlyContinue) {
    return
  }
  Set-ExecutionPolicy Bypass -Scope Process -Force
  [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
  iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
  if (-not (Get-Command choco -ErrorAction SilentlyContinue)) {
    throw 'Chocolatey installation did not provide choco.exe'
  }
}

function Assert-MsvcToolchainPresent {
  $vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
  if (-not (Test-Path $vcvars)) {
    throw "MSVC BuildTools not found (missing vcvars64.bat): $vcvars"
  }
}

# Install libarchive via vcpkg and expose environment variables for later steps.
if (-not $env:VCPKG_INSTALLATION_ROOT) {
  $env:VCPKG_INSTALLATION_ROOT = "C:\vcpkg"
}

if ($InContainer) {
  Ensure-Chocolatey

  # Tools used by build/test flows
  Invoke-Native choco @('install', '-y', 'git', 'cmake', 'ninja', 'python3', 'ruby')

  # MSVC toolchain
  # NOTE: Installing visualstudio2022buildtools directly is known to fail intermittently in Windows containers.
  # The vctools workload package pulls in Build Tools as a dependency; install the workload only.
  try {
    Invoke-Native choco @(
      'install', '-y', 'visualstudio2022-workload-vctools',
      '--execution-timeout', '7200',
      '--package-parameters', '"--includeRecommended --passive --norestart"'
    )
  } catch {
    Dump-InstallerLogs
    throw
  }

  Assert-MsvcToolchainPresent

  # Ensure vcpkg exists
  $vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
  if (-not (Test-Path $vcpkgRoot)) {
    $gitExe = Join-Path $env:ProgramFiles 'Git\cmd\git.exe'
    if (-not (Test-Path $gitExe)) {
      $gitExe = Join-Path $env:ProgramFiles 'Git\bin\git.exe'
    }
    if (-not (Test-Path $gitExe)) {
      throw "git.exe was not found after installation: $gitExe"
    }
    Invoke-Native $gitExe @('clone', 'https://github.com/microsoft/vcpkg.git', $vcpkgRoot)
  }

  Invoke-Native (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') @()
  $env:PATH = "$vcpkgRoot;$env:PATH"

  # Ensure this script can write outputs even outside GitHub Actions
  if (-not $env:GITHUB_ENV) {
    $env:GITHUB_ENV = 'C:\github_env.txt'
  }
}

Write-Host "Installing libarchive:x64-windows via vcpkg (Release only)..."
$vcpkgCmd = (Get-Command vcpkg -ErrorAction SilentlyContinue)
if ($vcpkgCmd) {
  Invoke-Native $vcpkgCmd.Path @('install', 'libarchive:x64-windows-release', '--overlay-triplets=.', '--clean-after-build')
} else {
  $vcpkgExe = Join-Path $env:VCPKG_INSTALLATION_ROOT 'vcpkg.exe'
  if (-not (Test-Path $vcpkgExe)) {
    throw "vcpkg executable not found: $vcpkgExe"
  }
  Invoke-Native $vcpkgExe @('install', 'libarchive:x64-windows-release', '--overlay-triplets=.', '--clean-after-build')
}

$prefix = Join-Path $env:VCPKG_INSTALLATION_ROOT 'installed' | Join-Path -ChildPath 'x64-windows-release'
if (-not (Test-Path $prefix)) {
  throw "vcpkg prefix not found: $prefix"
}

"LIBARCHIVE_ROOT=$prefix" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"CMAKE_PREFIX_PATH=$prefix" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"PATH=$prefix\bin;$($env:PATH)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"LIB=$prefix\lib;$($env:LIB)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"INCLUDE=$prefix\include;$($env:INCLUDE)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append

if ($InContainer) {
  # Persist important variables into the image environment (Machine scope)
  [Environment]::SetEnvironmentVariable('LIBARCHIVE_ROOT', $prefix, [EnvironmentVariableTarget]::Machine)
  [Environment]::SetEnvironmentVariable('CMAKE_PREFIX_PATH', $prefix, [EnvironmentVariableTarget]::Machine)
  $newPath = "$prefix\bin;$env:PATH"
  [Environment]::SetEnvironmentVariable('PATH', $newPath, [EnvironmentVariableTarget]::Machine)
}
