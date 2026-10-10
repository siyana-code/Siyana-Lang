<#
.SYNOPSIS
    Build the Siyana Lang bootstrap compiler.

.DESCRIPTION
    Phase 1 exit criteria require a clean clone to build with one documented
    command. This script is that command on Windows.

    Requires only a C11 compiler. No build system generator, no external
    dependencies. A POSIX version lives in tools/build.sh.

.EXAMPLE
    ./tools/build.ps1
    ./tools/build.ps1 release
    ./tools/build.ps1 clean
    ./tools/build.ps1 test
#>

[CmdletBinding()]
param(
    [ValidateSet('debug', 'release', 'clean', 'test')]
    [string]$Config = 'debug'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root 'build'
$Exe = 'siyana.exe'

function Resolve-Compiler {
    # Honour $env:CC if set; otherwise take the first available compiler.
    if ($env:CC) { return $env:CC }
    foreach ($candidate in @('clang', 'gcc')) {
        if (Get-Command $candidate -ErrorAction SilentlyContinue) { return $candidate }
    }
    throw 'No C compiler found. Install GCC or Clang, or set $env:CC.'
}

switch ($Config) {
    'debug'   { $CFlags = @('-std=c11', '-O0', '-g', '-Wall', '-Wextra', '-Wpedantic') }
    'release' { $CFlags = @('-std=c11', '-O2', '-DNDEBUG', '-Wall', '-Wextra', '-Wpedantic') }
    'clean' {
        if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
        Write-Host 'cleaned'
        exit 0
    }
    'test' { $CFlags = @('-std=c11', '-O0', '-g', '-Wall', '-Wextra', '-Wpedantic') }
}

if ($Config -ne 'test') {
    $CC = Resolve-Compiler
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

    $Sources = @(
        (Join-Path $Root 'bootstrap/main.c'),
        (Join-Path $Root 'bootstrap/version.c')
    )

    foreach ($s in $Sources) {
        if (-not (Test-Path $s)) { throw "Missing source file: $s" }
    }

    $OutExe = Join-Path $BuildDir $Exe
    Write-Host "building siyana ($Config) with $CC"
    Write-Host "  $($CFlags -join ' ')"

    & $CC @CFlags '-I' (Join-Path $Root 'bootstrap/include') @Sources '-o' $OutExe
    if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }

    Write-Host "built: $OutExe"
}

# ---- Tests -----------------------------------------------------------------
# Phase 1 has no language tests yet; the suite covers the CLI contract.
# Language tests arrive in Phase 2 and are added alongside it.

if ($Config -eq 'test' -or $Config -ne 'clean') {
    $testScript = Join-Path $Root 'tests/cli_contract.ps1'
    if (-not (Test-Path $testScript)) { throw "Missing test script: $testScript" }
    & $testScript
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
