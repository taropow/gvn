param([Parameter(Mandatory=$true)][string]$BuildDirectory,[switch]$DocsOnly,[switch]$SkipDailyUpdate)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$projectText=Get-Content -LiteralPath (Join-Path $taskRoot 'CMakeLists.txt') -Raw
if($projectText -notmatch 'project\(GVN VERSION ([0-9]+\.[0-9]+\.[0-9]+) '){throw 'Project version not found'}
$packageVersion=$Matches[1]
$output=Join-Path $taskRoot ('dist\GVN-'+$packageVersion)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$exe=Join-Path $BuildDirectory 'Release\GVN.exe'
if (-not (Test-Path -LiteralPath $exe)) {throw 'Release executable is missing'}
$destinationExe=Join-Path $output 'GVN.exe'
if($DocsOnly){
    if(-not(Test-Path -LiteralPath $destinationExe)){throw 'Distribution executable is missing'}
    if((Get-FileHash -LiteralPath $exe).Hash -ne (Get-FileHash -LiteralPath $destinationExe).Hash){throw 'Distribution binary differs from build; run without DocsOnly'}
}else{Copy-Item -LiteralPath $exe -Destination $destinationExe}
Copy-Item -LiteralPath (Join-Path $taskRoot 'README.md') -Destination $output
Copy-Item -LiteralPath (Join-Path $taskRoot 'src\assets\gvn.ico') -Destination (Join-Path $output 'app.ico')
Copy-Item -LiteralPath (Join-Path $taskRoot 'docs') -Destination $output -Recurse -Force
$licenses=Join-Path $output 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party\libplum\LICENSE') -Destination (Join-Path $licenses 'libplum.txt')
Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party\psd_sdk\LICENSE') -Destination (Join-Path $licenses 'psd_sdk.txt')
Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party\miniz\LICENSE') -Destination (Join-Path $licenses 'miniz.txt')
Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party\libpng\LICENSE') -Destination (Join-Path $licenses 'libpng.txt')
Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party\zlib\LICENSE') -Destination (Join-Path $licenses 'zlib.txt')
Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party\libjpeg-turbo\LICENSE.md') -Destination (Join-Path $licenses 'libjpeg-turbo.txt')
Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party\libjpeg-turbo\README.ijg') -Destination (Join-Path $licenses 'libjpeg-IJG.txt')
Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party\giflib\COPYING') -Destination (Join-Path $licenses 'giflib.txt')
@'
GIFLIB 6.1.3: SPDX-License-Identifier: MIT
Copyright (C) Eric S. Raymond <esr@thyrsus.com>
openbsd-reallocarray.c: SPDX-License-Identifier: MIT
Copyright (C) 2008 Otto Moerbeek <otto@drijf.net>
The permission and warranty terms are in giflib.txt.
'@ | Set-Content -LiteralPath (Join-Path $licenses 'giflib-notices.txt') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party\manifest.json') -Destination (Join-Path $licenses 'manifest.json')
Get-FileHash -LiteralPath (Join-Path $output 'GVN.exe') -Algorithm SHA256 | ForEach-Object {($_.Hash.ToLower()+'  GVN.exe') | Set-Content -LiteralPath (Join-Path $output 'SHA256.txt') -Encoding ascii}
Compress-Archive -LiteralPath $output -DestinationPath (Join-Path $taskRoot ('dist\GVN-'+$packageVersion+'.zip')) -Force
Write-Output $output
if (-not $DocsOnly -and -not $SkipDailyUpdate) {
    & (Join-Path $PSScriptRoot 'update_daily.ps1') -SourceDirectory $output
}
