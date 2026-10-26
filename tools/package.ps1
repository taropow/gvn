param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [switch]$DocsOnly,
    [switch]$SkipDailyUpdate,
    [string]$OutputRoot = ''
)
$ErrorActionPreference = 'Stop'
Import-Module Microsoft.PowerShell.Utility -ErrorAction Stop
Import-Module Microsoft.PowerShell.Archive -ErrorAction Stop
$taskRoot = Split-Path -Parent $PSScriptRoot
$projectText = Get-Content -LiteralPath (Join-Path $taskRoot 'CMakeLists.txt') -Raw
if ($projectText -notmatch 'project\(GVN VERSION ([0-9]+\.[0-9]+\.[0-9]+) ') { throw 'Project version not found' }
$packageVersion = $Matches[1]
if (-not $OutputRoot) { $OutputRoot = Join-Path $taskRoot 'dist' }
$distributionRoot = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
New-Item -ItemType Directory -Path $distributionRoot -Force | Out-Null
$packageName = 'GVN-'+$packageVersion
$output = Join-Path $distributionRoot $packageName
$archive = $output+'.zip'
$exe = Join-Path $BuildDirectory 'Release\GVN.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Release executable is missing' }
if ($DocsOnly) {
    $previousExe = Join-Path $output 'GVN.exe'
    if (-not (Test-Path -LiteralPath $previousExe -PathType Leaf)) { throw 'Distribution executable is missing' }
    if ((Get-FileHash -LiteralPath $exe).Hash -ne (Get-FileHash -LiteralPath $previousExe).Hash) { throw 'Distribution binary differs from build; run without DocsOnly' }
}

# Build a fresh allowlist instead of inheriting old output or developer docs.
$stage = Join-Path $distributionRoot ('.package-'+[guid]::NewGuid().ToString('N'))
$stagedOutput = Join-Path $stage $packageName
New-Item -ItemType Directory -Path $stagedOutput -Force | Out-Null
try {
    Copy-Item -LiteralPath $exe -Destination (Join-Path $stagedOutput 'GVN.exe')
    Copy-Item -LiteralPath (Join-Path $taskRoot 'docs\gvn-manual.md') -Destination (Join-Path $stagedOutput 'gvn-manual.md')
    Copy-Item -LiteralPath (Join-Path $taskRoot 'src\assets\gvn.ico') -Destination (Join-Path $stagedOutput 'app.ico')
    $licenses = Join-Path $stagedOutput 'licenses'
    New-Item -ItemType Directory -Path $licenses | Out-Null
    $licenseSources = [ordered]@{
        'libplum.txt' = 'third_party\libplum\LICENSE'
        'psd_sdk.txt' = 'third_party\psd_sdk\LICENSE'
        'miniz.txt' = 'third_party\miniz\LICENSE'
        'picojson.txt' = 'third_party\picojson\LICENSE'
        'libpng.txt' = 'third_party\libpng\LICENSE'
        'zlib.txt' = 'third_party\zlib\LICENSE'
        'libjpeg-turbo.txt' = 'third_party\libjpeg-turbo\LICENSE.md'
        'libjpeg-IJG.txt' = 'third_party\libjpeg-turbo\README.ijg'
        'giflib.txt' = 'third_party\giflib\COPYING'
        'manifest.json' = 'third_party\manifest.json'
    }
    foreach ($entry in $licenseSources.GetEnumerator()) {
        Copy-Item -LiteralPath (Join-Path $taskRoot $entry.Value) -Destination (Join-Path $licenses $entry.Key)
    }
    @'
GIFLIB 6.1.3: SPDX-License-Identifier: MIT
Copyright (C) Eric S. Raymond <esr@thyrsus.com>
openbsd-reallocarray.c: SPDX-License-Identifier: MIT
Copyright (C) 2008 Otto Moerbeek <otto@drijf.net>
The permission and warranty terms are in giflib.txt.
'@ | Set-Content -LiteralPath (Join-Path $licenses 'giflib-notices.txt') -Encoding utf8
    @"
GVN $packageVersion - Windows 10/11 x64

ZIPをフォルダーごと展開し、GVN.exeを起動してください。
インストーラー、Python、開発ツール、追加プラグインは不要です。
画像をウィンドウへドロップするか、Ctrl+Oで開けます。
F1、ツールバーの「？」、または「その他 → 操作説明」で説明書を開きます。

同梱ファイル
  GVN.exe        アプリ本体（アイコンを内蔵）
  gvn-manual.md  操作説明
  app.ico        固定ショートカット更新用のアイコン
  SHA256.txt     GVN.exeのチェックサム
  licenses       第三者ライブラリのライセンスと使用版

設定は %LOCALAPPDATA%\GVN\settings.ini に保存されます。
このZIPに開発者の設定・画像・履歴は含まれていません。
更新時はアプリを終了し、展開した新しい版のファイルへ差し替えてください。
gvn-manual.mdとlicensesも一緒に保管してください。
"@ | Set-Content -LiteralPath (Join-Path $stagedOutput 'はじめに.txt') -Encoding utf8
    $hash = (Get-FileHash -LiteralPath (Join-Path $stagedOutput 'GVN.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
    ($hash+'  GVN.exe') | Set-Content -LiteralPath (Join-Path $stagedOutput 'SHA256.txt') -Encoding ascii
    $expected = @('GVN.exe','gvn-manual.md','app.ico','SHA256.txt','はじめに.txt') + @($licenseSources.Keys | ForEach-Object { 'licenses\'+$_ }) + @('licenses\giflib-notices.txt')
    $actual = @(Get-ChildItem -LiteralPath $stagedOutput -File -Recurse | ForEach-Object { $_.FullName.Substring($stagedOutput.Length+1) })
    if (@(Compare-Object ($expected | Sort-Object) ($actual | Sort-Object)).Count) { throw 'Distribution contains unexpected or missing files' }
    if ($hash -ne (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()) { throw 'Staged executable checksum mismatch' }
    $stagedArchive = Join-Path $stage ($packageName+'.zip')
    Compress-Archive -LiteralPath $stagedOutput -DestinationPath $stagedArchive

    # Preserve existing generated packages for recovery, including stale extras.
    if ((Test-Path -LiteralPath $output) -or (Test-Path -LiteralPath $archive)) {
        $backup = Join-Path $distributionRoot ('.previous\'+[guid]::NewGuid().ToString('N'))
        foreach ($path in @($output,$archive,$backup)) {
            if (-not [IO.Path]::GetFullPath($path).StartsWith($distributionRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Package move escapes output root' }
        }
        New-Item -ItemType Directory -Path $backup -Force | Out-Null
        if (Test-Path -LiteralPath $output) { Move-Item -LiteralPath $output -Destination (Join-Path $backup $packageName) }
        if (Test-Path -LiteralPath $archive) { Move-Item -LiteralPath $archive -Destination (Join-Path $backup ($packageName+'.zip')) }
    }
    foreach ($path in @($stagedOutput,$stagedArchive,$output,$archive)) {
        if (-not [IO.Path]::GetFullPath($path).StartsWith($distributionRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Package move escapes output root' }
    }
    Move-Item -LiteralPath $stagedOutput -Destination $output
    Move-Item -LiteralPath $stagedArchive -Destination $archive
} finally {
    $resolvedStage = [IO.Path]::GetFullPath($stage)
    if ($resolvedStage.StartsWith($distributionRoot+'\',[StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedStage) -match '^\.package-[a-f0-9]{32}$' -and (Test-Path -LiteralPath $resolvedStage)) {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}
Write-Output $output
if (-not $DocsOnly -and -not $SkipDailyUpdate) {
    & (Join-Path $PSScriptRoot 'update_daily.ps1') -SourceDirectory $output
}
