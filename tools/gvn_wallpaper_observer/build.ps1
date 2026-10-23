$ErrorActionPreference = 'Stop'
$sourceDirectory = $PSScriptRoot
$repository = Split-Path (Split-Path $sourceDirectory -Parent) -Parent
$outputDirectory = Join-Path $repository '.cache\gvn-wallpaper-observer'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$environmentScript = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $environmentScript)) { throw 'Visual Studio Build Tools were not found.' }
$commands = @(
    "@call `"$environmentScript`" -arch=x64 -host_arch=x64 >nul"
    "@cl /nologo /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /W4 /O2 /MT /EHsc /LD `"$sourceDirectory\observer.cpp`" /link /DEF:`"$sourceDirectory\observer.def`" /OUT:observer.dll gdi32.lib user32.lib shell32.lib advapi32.lib ole32.lib windowscodecs.lib uuid.lib"
    '@if errorlevel 1 exit /b 1'
    "@cl /nologo /std:c++20 /DUNICODE /D_UNICODE /W4 /O2 /MT /EHsc `"$sourceDirectory\attach_owned.cpp`" /Fe:attach_owned.exe"
)
$commandPath = Join-Path $outputDirectory 'build.cmd'
$commands | Set-Content -LiteralPath $commandPath -Encoding ASCII
Push-Location $outputDirectory
try {
    & cmd /c $commandPath
    if ($LASTEXITCODE -ne 0) { throw "GVN observer build failed: $LASTEXITCODE" }
} finally { Pop-Location }
$sources = [ordered]@{}
foreach ($name in @('observer.cpp', 'observer.def', 'attach_owned.cpp', 'build.ps1')) {
    $sources[$name] = (Get-FileHash -LiteralPath (Join-Path $sourceDirectory $name) -Algorithm SHA256).Hash.ToLowerInvariant()
}
$binaries = [ordered]@{}
foreach ($name in @('observer.dll', 'attach_owned.exe')) {
    $binaries[$name] = (Get-FileHash -LiteralPath (Join-Path $outputDirectory $name) -Algorithm SHA256).Hash.ToLowerInvariant()
}
@{ sources = $sources; binaries = $binaries; architecture = 'x64'; clock_replacement = $false; wallpaper_setter_virtualized = $true; wallpaper_mutex_virtualized = $true } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputDirectory 'build.json') -Encoding UTF8
