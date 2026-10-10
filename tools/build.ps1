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
    # -Werror is on by default. This is a small C codebase where a warning is
    # almost always a real defect, and treating it as an error keeps the
    # bootstrap clean without needing a separate CI run to catch it.
    'debug'   { $CFlags = @('-std=c11', '-O0', '-g', '-Wall', '-Wextra', '-Wpedantic', '-Werror') }
    'release' { $CFlags = @('-std=c11', '-O2', '-DNDEBUG', '-Wall', '-Wextra', '-Wpedantic', '-Werror') }
    'clean' {
        if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
        Write-Host 'cleaned'
        exit 0
    }
    'test'    { $CFlags = @('-std=c11', '-O0', '-g', '-Wall', '-Wextra', '-Wpedantic', '-Werror') }
}

# Resolved unconditionally, not inside the build block below: the allocator
# prototype is compiled on every run, including `-Config test`, so leaving this
# to the build block left $CC unset for that path and the run died on a
# strict-mode error rather than doing anything useful.
$CC = Resolve-Compiler

if ($Config -ne 'test') {
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

    $Sources = @(
        (Join-Path $Root 'bootstrap/main.c'),
        (Join-Path $Root 'bootstrap/version.c'),
        (Join-Path $Root 'bootstrap/lexer.c'),
        (Join-Path $Root 'bootstrap/parser.c'),
        (Join-Path $Root 'bootstrap/interp.c'),
        (Join-Path $Root 'bootstrap/diagnostics.c'),
        (Join-Path $Root 'bootstrap/driver.c')
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
# The suite covers the CLI contract and the language slice. Both run on every
# build so a regression cannot land without a failing test.

if ($Config -eq 'test' -or $Config -ne 'clean') {
    # The allocator prototype is built and run as part of the suite. It backs
    # the lexically scoped model recommendation in
    # research/prototype/allocator-model.md, so it is checked continuously
    # rather than treated as a one-off experiment.
    $proto = Join-Path $Root 'prototype/allocator_proto.c'
    $protoExe = Join-Path $BuildDir 'allocator_proto.exe'
    if (-not (Test-Path $proto)) { throw "Missing prototype: $proto" }
    & $CC @CFlags $proto '-o' $protoExe
    if ($LASTEXITCODE -ne 0) { throw "allocator prototype failed to build" }
    & $protoExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host ''

    $suites = @(
        (Join-Path $Root 'tests/cli_contract.ps1'),
        (Join-Path $Root 'tests/language.ps1')
    )

    foreach ($suite in $suites) {
        if (-not (Test-Path $suite)) { throw "Missing test suite: $suite" }
        & $suite
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}
