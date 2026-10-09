# SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or -not [Environment]::Is64BitProcess) {
    throw 'Run this script in a 64-bit Windows PowerShell as administrator.'
}
$version = '0.1.0-preview1'
$destination = Join-Path $env:ProgramFiles "ChengyinIME\$version"
$dll = Join-Path $destination 'chengyin_tsf.dll'
$marker = Join-Path $destination 'chengyin-install.txt'
$registration = 'HKLM:\Software\Classes\CLSID\{65C32A54-219A-4F0A-B44C-B963D7BA532F}'
if (-not (Test-Path $destination)) {
    if (Test-Path $registration) { throw 'Registered DLL is outside this preview directory; refusing to alter it.' }
    Write-Host 'This preview is not installed.'
    exit 0
}
if (-not (Test-Path $marker) -or (Get-Content -LiteralPath $marker -Raw).Trim() -ne $version) {
    throw 'Installation marker does not match; refusing to remove this directory.'
}
if (Test-Path $registration) {
    $registered = (Get-Item -LiteralPath "$registration\InprocServer32").GetValue('')
    if (-not [String]::Equals($registered, $dll, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Another Chengyin installation is registered; refusing to unregister it.'
    }
    & "$env:SystemRoot\System32\regsvr32.exe" /u /s $dll
    if ($LASTEXITCODE -ne 0) { throw "Unregistration failed (regsvr32 exit $LASTEXITCODE). Files retained." }
}
try {
    # Remove only this preview's known files, never an arbitrary directory tree.
    foreach ($file in @('chengyin_tsf.dll', 'chengyin_probe.exe', 'chengyin_settings.exe', 'chengyin_testpad.exe', 'Uninstall.ps1', 'README.md', 'LICENSE', 'THIRD_PARTY.md', 'RUNTIME_LICENSES.zip', 'BUILD_INFO.json')) {
        $path = Join-Path $destination $file
        if (Test-Path $path) { Remove-Item -LiteralPath $path -Force }
    }
    Remove-Item -LiteralPath $marker -Force
    if (@(Get-ChildItem -LiteralPath $destination -Force).Count -eq 0) { Remove-Item -LiteralPath $destination }
    Write-Host 'Chengyin has been unregistered and its preview files removed. Sign out to refresh the input switcher.'
} catch {
    Write-Warning 'Chengyin is unregistered, but an application may still have its DLL open. Sign out, sign in, then run the copy of Uninstall.ps1 from the extracted ZIP again.'
    throw
}
