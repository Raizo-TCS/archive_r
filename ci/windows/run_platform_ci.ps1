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

$posixWorkspace = Convert-ToPosixPath -Path $workspace
$buildCommand = "./build.sh --rebuild-all --package-python --package-ruby"
$testCommand = "timeout 120 ./run_tests.sh"
$scriptBody = "set -euo pipefail; cd $posixWorkspace; $buildCommand; $testCommand"

switch ($Platform) {
    "pf5" {
        $bashExe = "C:\\Program Files\\Git\\bin\\bash.exe"
        if (-not (Test-Path -LiteralPath $bashExe)) {
            throw "bash.exe not found at $bashExe"
        }
        & $bashExe -lc $scriptBody
    }
    "pf6" {
        $bashExe = "C:\\tools\\msys64\\usr\\bin\\bash.exe"
        if (-not (Test-Path -LiteralPath $bashExe)) {
            throw "MSYS2 bash.exe not found at $bashExe"
        }
        $env:MSYSTEM = "UCRT64"
        $env:CHERE_INVOKING = "1"
        & $bashExe -lc $scriptBody
    }
    default {
        throw "Unsupported platform $Platform"
    }
}
