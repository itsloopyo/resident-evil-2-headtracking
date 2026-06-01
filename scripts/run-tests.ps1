#requires -Version 5.1
# Configure, build, and run the RE2 Head Tracking unit tests.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'build-tests'

cmake -B $buildDir -G 'Visual Studio 17 2022' -A x64 -DRE2HT_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

cmake --build $buildDir --config Debug --target re2ht_tests
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }

ctest --test-dir $buildDir -C Debug --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }

Write-Host "All tests passed" -ForegroundColor Green
