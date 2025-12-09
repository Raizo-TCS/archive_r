$ErrorActionPreference = 'Stop'

function Get-GitBashPath {
	$candidates = @(
		(Join-Path $Env:ProgramFiles 'Git\bin\bash.exe'),
		(Join-Path $Env:ProgramFiles 'Git\usr\bin\bash.exe'),
		(Join-Path ${Env:ProgramW6432} 'Git\bin\bash.exe'),
		(Join-Path ${Env:ProgramFiles(x86)} 'Git\bin\bash.exe')
	)
	foreach ($p in $candidates) {
		if ($p -and (Test-Path $p)) { return $p }
	}
	$cmd = Get-Command bash.exe -ErrorAction SilentlyContinue
	if ($cmd) { return $cmd.Source }
	throw "Git Bash not found. Install Git for Windows."
}

$repoRoot = $PSScriptRoot
$bash = Get-GitBashPath
$timeoutPy = Join-Path $repoRoot "run_with_timeout.py"
$python = (Get-Command python -ErrorAction Stop).Source
$findExe = Get-ChildItem -Path (Join-Path $repoRoot 'build') -Recurse -Filter 'find_and_traverse.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
$logDir = Join-Path $repoRoot "build/logs"
$testLog = Join-Path $logDir "msvc-run-tests.log"
$rubyLog = Join-Path $logDir "msvc-ruby-binding-tests.log"
$pythonLog = Join-Path $logDir "msvc-python-binding-tests.log"
$env:ARCHIVE_R_REPO_WIN = $repoRoot
$repoRootUnix = (& $bash -lc 'cygpath -u "$ARCHIVE_R_REPO_WIN"').Trim()

$vcpkgRoot = $env:LIBARCHIVE_ROOT
if ($vcpkgRoot) {
	$vcpkgBin = Join-Path $vcpkgRoot "bin"
	$env:VCPKG_BIN_WIN = $vcpkgBin
	$vcpkgBinUnix = (& $bash -lc 'cygpath -u "$VCPKG_BIN_WIN"').Trim()
	Write-Host "[msvc] Prepending vcpkg bin to PATH: $vcpkgBinUnix"
	
	# Copy DLLs to build directory to ensure they are found first (DLL Hell fix)
	$buildDir = Join-Path $repoRoot "build"
	Write-Host "[msvc] Copying vcpkg DLLs from $vcpkgBin to $buildDir"
	Get-ChildItem $vcpkgBin -Filter "*.dll" | Copy-Item -Destination $buildDir -Force
	Get-ChildItem $buildDir -Filter "*.dll" | ForEach-Object { Write-Host "[msvc] DLL in build: $($_.Name)" }

	$pathExport = 'export PATH="{1}:{0}/build/bindings/python/.libs:{0}/build/core:{0}/build/core/Release:{0}/build/Release:{0}/build:$PATH"' -f $repoRootUnix, $vcpkgBinUnix
} else {
	Write-Warning "[msvc] LIBARCHIVE_ROOT not set, using default PATH"
	$pathExport = 'export PATH="{0}/build/bindings/python/.libs:{0}/build/core:{0}/build/core/Release:{0}/build/Release:{0}/build:$PATH"' -f $repoRootUnix
}

function Invoke-WithLog {
	param(
		[string]$name,
		[string[]]$cmdArgs,
		[string]$logPath
	)

	Write-Host "[msvc] running $name (log: $logPath)"
	$null = New-Item -ItemType Directory -Path (Split-Path $logPath -Parent) -Force
	& $python $timeoutPy 120 @cmdArgs 2>&1 | Tee-Object -FilePath $logPath
	$exitCode = $LASTEXITCODE
	Write-Host "[msvc] $name exit code: $exitCode"
	if ($exitCode -ne 0) { throw "[msvc] $name failed with exit code $exitCode. See log: $logPath" }
}

Push-Location $repoRoot
try {
	Write-Host "[msvc] repo root: $repoRoot"
	Write-Host "[msvc] bash path: $bash"
	Write-Host "[msvc] python path: $python"
	Write-Host "[msvc] timeout helper: $timeoutPy"
	if (-not (Test-Path $bash)) { throw "[msvc] bash not found: $bash" }
	if (-not (Test-Path $timeoutPy)) { throw "[msvc] timeout helper not found: $timeoutPy" }
	if ($findExe) {
		Write-Host "[msvc] found find_and_traverse.exe: $($findExe.FullName)"
		& $findExe.FullName --help *>$null
		Write-Host "[msvc] smoke exit: $LASTEXITCODE"
	} else {
		Write-Host "[msvc] find_and_traverse.exe not found under build/"
	}

	$bashPrefix = @($bash, '-lc')
	$envDump = @(
		'set -eo pipefail'
		$pathExport
		'echo "[msvc/bash] uname: $(uname -a)"'
		'echo "[msvc/bash] PATH=$PATH"'
		'echo "[msvc/bash] which timeout: $(which timeout)"'
		'echo "[msvc/bash] timeout version: $(timeout --version || echo "not gnu timeout")"'
		'echo "[msvc/bash] DLLs in build:"'
		'find build -name "*.dll"'
		'echo "[msvc/bash] Executable location:"'
		'find build -name "find_and_traverse.exe"'
		'echo "[msvc/bash] ldd on executable:"'
		'ldd $(find build -name "find_and_traverse.exe" | head -n 1) || echo "ldd failed"'
		'pwd'
	) -join '; '

	Invoke-WithLog "run_tests.sh" ($bashPrefix + @("$envDump; ./run_tests.sh")) $testLog
	Invoke-WithLog "ruby binding tests" ($bashPrefix + @("$envDump; ./bindings/ruby/run_binding_tests.sh")) $rubyLog
	Invoke-WithLog "python binding tests" ($bashPrefix + @("$envDump; ./bindings/python/run_binding_tests.sh")) $pythonLog
} finally {
	Pop-Location
}
