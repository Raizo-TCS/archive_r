$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
$bash = (Get-Command bash.exe -ErrorAction Stop).Source
Push-Location $repoRoot
try {
	& $bash ./.github/scripts/ci/run_with_timeout.py 120 ./run_tests.sh
	& $bash ./.github/scripts/ci/run_with_timeout.py 120 ./bindings/ruby/run_binding_tests.sh
	& $bash ./.github/scripts/ci/run_with_timeout.py 120 ./bindings/python/run_binding_tests.sh
} finally {
	Pop-Location
}
