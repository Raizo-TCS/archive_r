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
	& $python $timeoutPy 120 $bash ./run_tests.sh
	& $python $timeoutPy 120 $bash ./bindings/ruby/run_binding_tests.sh
	& $python $timeoutPy 120 $bash ./bindings/python/run_binding_tests.sh
} finally {
	Pop-Location
}
