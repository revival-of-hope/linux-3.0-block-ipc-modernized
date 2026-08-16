$ErrorActionPreference = "Stop"

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Here "build"

Write-Host "[1/5] Checking required commands"
foreach ($Command in @("cmake", "python")) {
    if (-not (Get-Command $Command -ErrorAction SilentlyContinue)) {
        throw "Missing command: $Command"
    }
}

Write-Host "[2/5] Configuring a clean native C build"
if (Test-Path $Build) {
    Remove-Item -Recurse -Force $Build
}
cmake -S $Here -B $Build

Write-Host "[3/5] Compiling portable policy tests"
cmake --build $Build --config Release

Write-Host "[4/5] Running portable policy tests"
ctest --test-dir $Build -C Release --output-on-failure

Write-Host "[5/5] Checking refactored kernel source contracts"
python (Join-Path $Here "source_contract_test.py")

Write-Host "PASS: native Windows verification completed"
