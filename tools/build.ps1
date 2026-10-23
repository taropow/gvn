param([string]$Configuration = 'Release', [string]$BuildDirectory = '', [switch]$NoTest)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $taskRoot 'build' }
& cmake -S $taskRoot -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& cmake --build $BuildDirectory --config $Configuration --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
if (-not $NoTest) {
    & ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure --output-junit (Join-Path $BuildDirectory 'ctest-results.xml')
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
}
