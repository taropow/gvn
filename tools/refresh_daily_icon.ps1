param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Icon
)
$ErrorActionPreference = 'Stop'
$exePath = (Resolve-Path -LiteralPath $Executable).ProviderPath
$iconPath = (Resolve-Path -LiteralPath $Icon).ProviderPath
$taskRoot = Split-Path -Parent $PSScriptRoot
$pinned = Join-Path $env:APPDATA 'Microsoft\Internet Explorer\Quick Launch\User Pinned\TaskBar'
$shell = New-Object -ComObject WScript.Shell
$changed = @()
if (-not ('GvnIconRefresh' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class GvnIconRefresh {
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
    public static extern void SHChangeNotify(uint e, uint f, string p1, IntPtr p2);
}
'@
}
if (Test-Path -LiteralPath $pinned) {
    foreach ($file in Get-ChildItem -LiteralPath $pinned -Filter '*.lnk' -File) {
        $link = $shell.CreateShortcut($file.FullName)
        if ($link.TargetPath -ine $exePath) { continue }
        $location = $iconPath+',0'
        if ($link.IconLocation -eq $location) { continue }
        $backup = Join-Path $taskRoot ('.cache\shortcut-backups\'+[guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $backup -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $backup $file.Name)
        # Preserve the target, arguments and all other shortcuts; repair only this existing pin.
        $link.IconLocation = $location
        $link.Save()
        $verified = $shell.CreateShortcut($file.FullName)
        if ($verified.TargetPath -ine $exePath -or $verified.IconLocation -ne $location) {
            throw 'Pinned shortcut verification failed'
        }
        [GvnIconRefresh]::SHChangeNotify(0x2000,0x1005,$file.FullName,[IntPtr]::Zero)
        $changed += $file.FullName
    }
}
[GvnIconRefresh]::SHChangeNotify(0x2000,0x1005,$exePath,[IntPtr]::Zero)
[ordered]@{existing_pins_updated=$changed;icon=$iconPath;explorer_restarted=$false} | ConvertTo-Json -Compress
