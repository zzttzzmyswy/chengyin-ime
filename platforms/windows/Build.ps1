[CmdletBinding()]
param([string]$MakeNsis = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Push-Location $root
try {
    # NSIS 3.11+ is needed only on the build machine, never on the user's PC.
    if (-not $MakeNsis) {
        $command = Get-Command makensis.exe -ErrorAction SilentlyContinue
        if ($null -ne $command) { $MakeNsis = $command.Source }
        else { $MakeNsis = Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe' }
    }
    if (-not (Test-Path -LiteralPath $MakeNsis)) { throw 'Install NSIS 3.11+ or pass -MakeNsis.' }
    $nsisVersion = (& $MakeNsis /VERSION).Trim()
    if ($nsisVersion -notmatch '^v(\d+\.\d+)' -or [version]$Matches[1] -lt [version]'3.11') {
        throw 'Native x64 installers require NSIS 3.11+.'
    }
    & rustup target add x86_64-pc-windows-msvc
    if ($LASTEXITCODE -ne 0) { throw 'rustup failed' }
    & rustup component add rust-docs
    if ($LASTEXITCODE -ne 0) { throw 'Rust runtime license documentation is unavailable' }
    $previousFlags = $env:RUSTFLAGS
    try {
        $env:RUSTFLAGS = '-C target-feature=+crt-static'
        & cargo build --release -p myswy-ffi --target x86_64-pc-windows-msvc --locked
        if ($LASTEXITCODE -ne 0) { throw 'Rust build failed' }
    } finally { $env:RUSTFLAGS = $previousFlags }
    & cmake -S platforms/windows -B build/windows-msvc -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    & cmake --build build/windows-msvc --config Release
    if ($LASTEXITCODE -ne 0) { throw 'C++ build failed' }
    & ctest --test-dir build/windows-msvc -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Windows tests failed' }
    & python scripts/package_windows.py --build-dir build/windows-msvc --toolchain msvc --makensis $MakeNsis --nsis-notice packaging/windows/NSIS-LICENSE.txt
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
} finally { Pop-Location }
