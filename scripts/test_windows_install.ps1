# Run only on an isolated Windows CI runner. Registers this preview temporarily.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Package)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$registration = 'HKLM:\Software\Classes\CLSID\{65C32A54-219A-4F0A-B44C-B963D7BA532F}'
$destination = Join-Path $env:ProgramFiles 'ChengyinIME\0.1.0-preview1'
if ((Test-Path $registration) -or (Test-Path $destination)) { throw 'Use a clean isolated Windows runner.' }
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('chengyin-install-' + [Guid]::NewGuid().ToString('N'))
Expand-Archive -LiteralPath (Resolve-Path $Package).Path -DestinationPath $temporary
try {
    foreach ($script in @('Install.ps1', 'Uninstall.ps1')) {
        $tokens = $null; $errors = $null
        [void][Management.Automation.Language.Parser]::ParseFile((Join-Path $temporary $script), [ref]$tokens, [ref]$errors)
        if ($errors.Count -gt 0) { throw "PowerShell parse failed: $script" }
    }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $temporary 'Install.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Install failed' }
    $dll = Join-Path $destination 'chengyin_tsf.dll'
    $registered = (Get-Item -LiteralPath "$registration\InprocServer32").GetValue('')
    if ($registered -ne $dll) { throw 'Registered path mismatch' }
    & (Join-Path $destination 'chengyin_probe.exe') $dll --registered
    if ($LASTEXITCODE -ne 0) { throw 'Registered COM activation failed' }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $temporary 'Install.ps1')
    if ($LASTEXITCODE -eq 0) { throw 'Duplicate install should be rejected' }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $temporary 'Uninstall.ps1')
    if ($LASTEXITCODE -ne 0 -or (Test-Path $registration) -or (Test-Path $destination)) { throw 'Uninstall did not clean this preview' }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $temporary 'Uninstall.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Repeated uninstall should succeed' }
    Write-Host 'PASS: package parse, install, registered COM, duplicate rejection, uninstall and repeated cleanup.'
} finally {
    if ((Test-Path $registration) -or (Test-Path $destination)) {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $temporary 'Uninstall.ps1')
    }
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
