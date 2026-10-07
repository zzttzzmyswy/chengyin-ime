# Run in an elevated, 64-bit Windows PowerShell. Changes only Chengyin's registration.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$admin = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $admin.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Open Windows PowerShell as administrator, then run this script again.'
}
if (-not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') {
    throw 'This preview requires Intel/AMD x64 Windows and a 64-bit PowerShell.'
}
if ([Environment]::OSVersion.Version.Build -lt 19041) { throw 'Windows 10 version 2004 or newer is required.' }
$version = '0.1.0-preview1'
$destination = Join-Path $env:ProgramFiles "ChengyinIME\$version"
$registration = 'HKLM:\Software\Classes\CLSID\{65C32A54-219A-4F0A-B44C-B963D7BA532F}'
if (Test-Path $registration) { throw 'Chengyin is already registered. Uninstall the previous preview and sign out before reinstalling.' }
if (Test-Path $destination) { throw "Directory already exists: $destination. Use Uninstall.ps1 to clean the previous preview first." }
$files = @('chengyin_tsf.dll', 'chengyin_probe.exe', 'chengyin_settings.exe', 'Uninstall.ps1', 'README.md', 'LICENSE', 'THIRD_PARTY.md', 'RUNTIME_LICENSES.zip', 'BUILD_INFO.json')
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'SHA256SUMS.json') -Raw | ConvertFrom-Json
foreach ($file in $files) {
    $expected = $manifest.PSObject.Properties[$file]
    if ($null -eq $expected -or (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot $file)).Hash -ne $expected.Value) {
        throw "Missing file or SHA-256 mismatch: $file"
    }
}
# Run a read-only COM probe before changing the machine registration.
& (Join-Path $PSScriptRoot 'chengyin_probe.exe') (Join-Path $PSScriptRoot 'chengyin_tsf.dll')
if ($LASTEXITCODE -ne 0) { throw 'The COM/TSF probe failed; nothing has been installed.' }
New-Item -ItemType Directory -Path $destination | Out-Null
try {
    foreach ($file in $files) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $destination }
    Set-Content -LiteralPath (Join-Path $destination 'chengyin-install.txt') -Value $version -Encoding ASCII
    & "$env:SystemRoot\System32\regsvr32.exe" /s (Join-Path $destination 'chengyin_tsf.dll')
    if ($LASTEXITCODE -ne 0) { throw "TSF registration failed (regsvr32 exit $LASTEXITCODE)." }
} catch {
    # The DLL rolls back partial registration. Retain files if any registration remains.
    if (-not (Test-Path $registration)) { Remove-Item -LiteralPath $destination -Recurse -Force }
    throw
}
Write-Host "Installed to $destination"
Write-Host 'Sign out and sign in. Add Chinese (Simplified, China) in Language settings if needed.'
Write-Host 'Use Win+Space to select Chengyin, then open chengyin_settings.exe normally (not as administrator).'
Write-Host 'This is a 98-entry demo dictionary. Read README.md for scope and test steps.'
