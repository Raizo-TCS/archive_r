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
Push-Location $repoRoot
try {
	Write-Host "[msvc] repo root: $repoRoot"
	Write-Host "[msvc] bash path: $bash"
	Write-Host "[msvc] python path: $python"
	Write-Host "[msvc] timeout helper: $timeoutPy"
	if (-not (Test-Path $bash)) { throw "[msvc] bash not found: $bash" }
	if (-not (Test-Path $timeoutPy)) { throw "[msvc] timeout helper not found: $timeoutPy" }

	& $python $timeoutPy 120 $bash ./run_tests.sh
	if ($LASTEXITCODE -ne 0) { throw "[msvc] run_tests.sh failed with exit code $LASTEXITCODE" }

	& $python $timeoutPy 120 $bash ./bindings/ruby/run_binding_tests.sh
	if ($LASTEXITCODE -ne 0) { throw "[msvc] ruby binding tests failed with exit code $LASTEXITCODE" }

	& $python $timeoutPy 120 $bash ./bindings/python/run_binding_tests.sh
	if ($LASTEXITCODE -ne 0) { throw "[msvc] python binding tests failed with exit code $LASTEXITCODE" }
} finally {
	Pop-Location
}
