param(
    [string]$SourceDirectory = '',
    [string]$InstallDirectory = '',
    [switch]$Worker
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if (-not $InstallDirectory) { $InstallDirectory = Join-Path $taskRoot 'app' }
$install = [IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
$requestPath = Join-Path $install '.pending-update.json'
$resultPath = Join-Path $install 'last-pending-update.json'
if (-not $Worker) {
    $source = (Resolve-Path -LiteralPath $SourceDirectory).ProviderPath
    $exe = Join-Path $source 'GVN.exe'
    $version = (Get-Item -LiteralPath $exe).VersionInfo
    $hash = (Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant()
    $checksum = (Get-Content -LiteralPath (Join-Path $source 'SHA256.txt') -Raw).Trim()
    if ($version.ProductName -ne 'GVN Image Viewer' -or $checksum -ne ($hash+'  GVN.exe') -or
        -not (Test-Path -LiteralPath (Join-Path $source 'licenses') -PathType Container)) { throw 'Invalid GVN package' }
    New-Item -ItemType Directory -Path $install -Force | Out-Null
    $request = [ordered]@{source=$source;sha256=$hash;version=$version.FileVersion;queued_at=[DateTime]::UtcNow.ToString('o')}
    $temporary = Join-Path $install ('.pending-'+[guid]::NewGuid().ToString('N')+'.json')
    $request | ConvertTo-Json | Set-Content -LiteralPath $temporary -Encoding utf8
    if (Test-Path -LiteralPath $requestPath) { [IO.File]::Replace($temporary,$requestPath,$null) }
    else { [IO.File]::Move($temporary,$requestPath) }
    $script = $PSCommandPath.Replace("'","''")
    $argument = $install.Replace("'","''")
    $command = "& '$script' -Worker -InstallDirectory '$argument'"
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $hostExe = Join-Path $PSHOME 'pwsh.exe'
    if (-not (Test-Path -LiteralPath $hostExe)) { $hostExe = Join-Path $PSHOME 'powershell.exe' }
    $process = Start-Process -FilePath $hostExe -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded) -WindowStyle Hidden -PassThru
    [ordered]@{status='Queued';path=(Join-Path $install 'gvd.exe');requested_version=$version.FileVersion;worker_pid=$process.Id;request=$requestPath;result=$resultPath} | ConvertTo-Json -Compress
    exit
}
# One transient worker per installation. A later queue replaces the request,
# so an existing worker applies the newest package after the viewer closes.
$digest = [Convert]::ToBase64String([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($install.ToLowerInvariant()))).Replace('/','_').Replace('+','-')
$mutex = New-Object Threading.Mutex($false,('Local\GVN.PendingUpdate.'+$digest))
if (-not $mutex.WaitOne(0)) { $mutex.Dispose(); exit }
try {
    $target = Join-Path $install 'gvd.exe'
    while (Test-Path -LiteralPath $requestPath) {
        $running = @(Get-Process -Name gvd -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $target })
        if ($running.Count) {
            foreach ($process in $running) { try { $null=$process.WaitForExit(1000) } catch {} }
            continue
        }
        $request = Get-Content -LiteralPath $requestPath -Raw | ConvertFrom-Json
        if ((Get-FileHash -LiteralPath (Join-Path $request.source 'GVN.exe')).Hash.ToLowerInvariant() -ne $request.sha256) { throw 'Pending package changed' }
        $result = & (Join-Path $PSScriptRoot 'update_daily.ps1') -SourceDirectory $request.source -InstallDirectory $install | ConvertFrom-Json
        if ($result.status -eq 'Deferred') { Start-Sleep -Milliseconds 250; continue }
        if ($result.status -notin @('Updated','AlreadyCurrent')) { throw 'Unexpected update result' }
        $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resultPath -Encoding utf8
        $current = Get-Content -LiteralPath $requestPath -Raw | ConvertFrom-Json
        if ($current.sha256 -eq $request.sha256) { Remove-Item -LiteralPath $requestPath; break }
    }
} catch {
    [ordered]@{status='Failed';error=$_.Exception.Message;recorded_at=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding utf8
    exit 1
} finally { $mutex.ReleaseMutex(); $mutex.Dispose() }
