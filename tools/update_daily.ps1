param(
    [Parameter(Mandatory=$true)][string]$SourceDirectory,
    [string]$InstallDirectory = '',
    [switch]$PreserveRunning
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if (-not $InstallDirectory) { $InstallDirectory = Join-Path $taskRoot 'app' }
$source = (Resolve-Path -LiteralPath $SourceDirectory).ProviderPath.TrimEnd('\')
$install = [IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
if ($install -ieq $source -or $install.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase) -or
    $source.StartsWith($install+'\',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'The daily installation and distribution must be separate directories'
}
$lockDigest = [Convert]::ToBase64String([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($install.ToLowerInvariant()))).Replace('/','_').Replace('+','-')
$updateMutex = New-Object Threading.Mutex($false,('Local\GVN.InstallUpdate.'+$lockDigest))
if (-not $updateMutex.WaitOne(10000)) { $updateMutex.Dispose(); throw 'Another GVN installation update is still active' }
try {
$sourceExe = Join-Path $source 'GVN.exe'
$sourceChecksum = Join-Path $source 'SHA256.txt'
$sourceLicenses = Join-Path $source 'licenses'
$sourceManual = Join-Path $source 'gvn-manual.md'
if (-not (Test-Path -LiteralPath $sourceExe -PathType Leaf) -or
    -not (Test-Path -LiteralPath $sourceChecksum -PathType Leaf) -or
    -not (Test-Path -LiteralPath $sourceLicenses -PathType Container)) {
    throw 'A packaged GVN.exe, SHA256.txt and licenses directory are required'
}
$checksum = (Get-Content -LiteralPath $sourceChecksum -Raw).Trim()
if ($checksum -notmatch '^([a-fA-F0-9]{64})\s+GVN\.exe$') { throw 'Invalid distribution checksum' }
$expectedHash = $Matches[1].ToLowerInvariant()
if ((Get-FileHash -LiteralPath $sourceExe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
    throw 'Distribution executable checksum mismatch; daily installation untouched'
}
$version = (Get-Item -LiteralPath $sourceExe).VersionInfo
if ($version.ProductName -ne 'GVN Image Viewer' -or $version.FileVersion -notmatch '^\d+\.\d+\.\d+$') {
    throw 'Not a versioned GVN executable'
}
if ([version]$version.FileVersion -ge [version]'0.76.4' -and -not (Test-Path -LiteralPath $sourceManual -PathType Leaf)) {
    throw 'gvn-manual.md is required for this version; daily installation untouched'
}
$licenseFiles = @(Get-ChildItem -LiteralPath $sourceLicenses -File -Recurse)
if ($licenseFiles.Count -eq 0) { throw 'Distribution licenses are empty' }
$targetExe = Join-Path $install 'gvd.exe'
$sameExe = (Test-Path -LiteralPath $targetExe -PathType Leaf) -and
    (Get-FileHash -LiteralPath $targetExe -Algorithm SHA256).Hash.ToLowerInvariant() -eq $expectedHash
if (-not $sameExe) {
    $running = @(Get-Process -Name gvd -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $targetExe })
    if ($running.Count -gt 0 -and -not $PreserveRunning) {
        [ordered]@{status='Deferred';reason='Daily viewer is running; close it and run the update again';path=$targetExe;running_pids=@($running.Id);requested_version=$version.FileVersion} | ConvertTo-Json -Compress
        return
    }
}
New-Item -ItemType Directory -Path $install -Force | Out-Null
$stage = Join-Path $install ('.update-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    Copy-Item -LiteralPath $sourceExe -Destination (Join-Path $stage 'gvd.exe')
    if ((Get-FileHash -LiteralPath (Join-Path $stage 'gvd.exe')).Hash.ToLowerInvariant() -ne $expectedHash) {
        throw 'Staged executable checksum mismatch'
    }
    $stagedLicenses = Join-Path $stage 'licenses'
    if (Test-Path -LiteralPath $sourceManual -PathType Leaf) {
        $manualHash = (Get-FileHash -LiteralPath $sourceManual -Algorithm SHA256).Hash
        $stagedManual = Join-Path $stage 'gvn-manual.md'
        Copy-Item -LiteralPath $sourceManual -Destination $stagedManual
        if ((Get-FileHash -LiteralPath $stagedManual -Algorithm SHA256).Hash -ne $manualHash) { throw 'Staged manual checksum mismatch' }
        $installedManual = Join-Path $install 'gvn-manual.md'
        if ((Test-Path -LiteralPath $installedManual -PathType Leaf) -and
            (Get-FileHash -LiteralPath $installedManual -Algorithm SHA256).Hash -ne $manualHash) {
            $manualBackup = Join-Path $install '.previous'
            New-Item -ItemType Directory -Path $manualBackup -Force | Out-Null
            Copy-Item -LiteralPath $installedManual -Destination (Join-Path $manualBackup 'gvn-manual.md') -Force
        }
        Copy-Item -LiteralPath $stagedManual -Destination $installedManual -Force
        if ((Get-FileHash -LiteralPath $installedManual -Algorithm SHA256).Hash -ne $manualHash) { throw 'Installed manual checksum mismatch' }
    }
    New-Item -ItemType Directory -Path $stagedLicenses | Out-Null
    foreach ($file in $licenseFiles) {
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked license files are not supported' }
        $relative = $file.FullName.Substring($sourceLicenses.Length+1)
        $destination = Join-Path $stagedLicenses $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination
        if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) {
            throw 'Staged license checksum mismatch'
        }
    }
    # Only managed files are updated. Local images, settings.ini and other files stay in place.
    foreach ($file in Get-ChildItem -LiteralPath $stagedLicenses -File -Recurse) {
        $relative = $file.FullName.Substring($stage.Length+1)
        $destination = Join-Path $install $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }
    if (-not $sameExe) {
        if (Test-Path -LiteralPath $targetExe -PathType Leaf) {
            $backup = Join-Path $install '.previous'
            New-Item -ItemType Directory -Path $backup -Force | Out-Null
            if ($PreserveRunning -and $running.Count -gt 0) {
                # Windows keeps the mapped executable alive after a rename. Preserve all
                # windows, edits and clipboard state; subsequent launches use the new file.
                $runningBackup = Join-Path $backup ('gvd-running-'+[guid]::NewGuid().ToString('N')+'.exe')
                $priorHash = (Get-FileHash -LiteralPath $targetExe).Hash
                [IO.File]::Move($targetExe,$runningBackup)
                try { [IO.File]::Move((Join-Path $stage 'gvd.exe'),$targetExe) }
                catch {
                    if (-not (Test-Path -LiteralPath $targetExe)) { [IO.File]::Move($runningBackup,$targetExe) }
                    throw
                }
                if ((Get-FileHash -LiteralPath $runningBackup).Hash -ne $priorHash) { throw 'Running executable backup checksum mismatch' }
            } else {
                [IO.File]::Replace((Join-Path $stage 'gvd.exe'),$targetExe,(Join-Path $backup 'gvd.exe'))
            }
        } else {
            [IO.File]::Move((Join-Path $stage 'gvd.exe'),$targetExe)
        }
    }
    if ((Get-FileHash -LiteralPath $targetExe).Hash.ToLowerInvariant() -ne $expectedHash) { throw 'Installed executable checksum mismatch' }
    $pinRefresh = $null
    $sourceIcon = Join-Path $source 'app.ico'
    if (Test-Path -LiteralPath $sourceIcon -PathType Leaf) {
        $iconHash = (Get-FileHash -LiteralPath $sourceIcon).Hash.ToLowerInvariant()
        $icons = Join-Path $install 'icons'
        New-Item -ItemType Directory -Path $icons -Force | Out-Null
        # A content-addressed filename bypasses the stale icon cached for the old EXE path.
        $installedIcon = Join-Path $icons ('gvn-'+$iconHash.Substring(0,16)+'.ico')
        Copy-Item -LiteralPath $sourceIcon -Destination $installedIcon -Force
        if ((Get-FileHash -LiteralPath $installedIcon).Hash.ToLowerInvariant() -ne $iconHash) { throw 'Installed icon checksum mismatch' }
        $pinRefresh = & (Join-Path $PSScriptRoot 'refresh_daily_icon.ps1') -Executable $targetExe -Icon $installedIcon | ConvertFrom-Json
    }
    ($expectedHash+'  gvd.exe') | Set-Content -LiteralPath (Join-Path $install 'SHA256.txt') -Encoding ascii
    @'
普段使い用 GVN Image Viewer

gvd.exeを起動してください。ショートカットや関連付けにはこの固定パスを使用できます。
設定は %LOCALAPPDATA%\GVN\settings.ini に保存し、従来の設定を引き継ぎます。
更新時も同じ場所のgvd.exeを差し替えます。使用中のアプリは強制終了しません。
使用中のため保留した更新は、gvd.exeを閉じてから開発側の更新手順を再実行します。
前の実行ファイルは、更新後に .previous\gvd.exe に残ります。
ライセンスはlicensesフォルダにあります。
操作説明はF1または「その他 → 操作説明」で開きます。同じフォルダーのgvn-manual.mdです。
'@ | Set-Content -LiteralPath (Join-Path $install 'はじめに.txt') -Encoding utf8
    $receipt = [ordered]@{status=$(if($sameExe){'AlreadyCurrent'}else{'Updated'});path=$targetExe;version=$version.FileVersion;sha256=$expectedHash;source=$source;updated_at_utc=[DateTime]::UtcNow.ToString('o');settings_path='%LOCALAPPDATA%\GVN\settings.ini'}
    if ($pinRefresh) { $receipt['icon_refresh'] = $pinRefresh }
    if ($manualHash) { $receipt['manual'] = @{path=$installedManual;sha256=$manualHash.ToLowerInvariant()} }
    if ($runningBackup) { $receipt['running_backup'] = $runningBackup; $receipt['preserved_running_pids'] = @($running.Id) }
    $receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $install 'installed.json') -Encoding utf8
    $receipt | ConvertTo-Json -Compress
} finally {
    # Delete only this invocation's generated staging directory, within the verified install root.
    $resolvedStage = [IO.Path]::GetFullPath($stage)
    if ($resolvedStage.StartsWith($install+'\',[StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedStage) -match '^\.update-[a-f0-9]{32}$' -and (Test-Path -LiteralPath $resolvedStage)) {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}

} finally { $updateMutex.ReleaseMutex(); $updateMutex.Dispose() }
