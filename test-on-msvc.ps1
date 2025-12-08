$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
$bash = (Get-Command bash.exe -ErrorAction Stop).Source
Push-Location $repoRoot
try {
	& python ./run_with_timeout.py 120 bash ./run_tests.sh
	& python ./run_with_timeout.py 120 bash ./bindings/ruby/run_binding_tests.sh
	& python ./run_with_timeout.py 120 bash ./bindings/python/run_binding_tests.sh
} finally {
	Pop-Location
}
