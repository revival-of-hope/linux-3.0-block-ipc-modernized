$ErrorActionPreference = "Stop"

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Here "build"

Write-Host "[1/4] Checking required commands"
foreach ($Command in @("cmake", "python")) {
    if (-not (Get-Command $Command -ErrorAction SilentlyContinue)) {
        throw "Missing command: $Command"
    }
}

Write-Host "[2/4] Configuring native Windows C build"
cmake -S $Here -B $Build

Write-Host "[3/4] Compiling and running C tests"
cmake --build $Build --config Release
ctest --test-dir $Build -C Release --output-on-failure

Write-Host "[4/4] Checking patched kernel source contracts"
python (Join-Path $Here "source_contract_test.py")

Write-Host "PASS: native Windows verification completed"
