# tools/leakcheck.ps1 -- measures live heap allocation, per loop iteration.
#
# No sanitizer is available on this toolchain (no libclang_rt.asan), which is
# why the struct/array leak in docs/architecture.md was recorded as reasoned
# from reading code rather than measured. This measures it instead, by linking
# the interpreter with GNU ld's --wrap so every malloc/free goes through a
# counter in tools/leakcheck.c. No interpreter source is changed.
#
# The measurement is differential. A program with a zero-iteration loop still
# allocates for its token list and AST, which legitimately live until exit, so
# the number that matters is live allocation per iteration of a loop that
# builds a value. Zero means the leak is gone.
#
# Usage:
#   .\tools\leakcheck.ps1              # measure every case
#   .\tools\leakcheck.ps1 -Only 1,2    # measure selected cases
#   .\tools\leakcheck.ps1 -Quiet       # report only failures
#
# Run by tools/build.ps1 on every build, so a leak cannot land silently. Exits
# non-zero when any case leaks, which is what makes that true.

param(
    [string[]]$Only = @(),
    [switch]$Quiet
)

$ErrorActionPreference = 'Continue'
$Root = Split-Path -Parent $PSScriptRoot
$OutDir = 'D:\Dev\Temp\opencode\leakcheck'

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$CC = if ($env:CC) { $env:CC } else { 'clang' }
$CFlags = @('-std=c11', '-O0', '-g', '-Wall', '-Wextra', '-Wpedantic', '-Werror')
$Wrap = @(
    '-Wl,--wrap=malloc',
    '-Wl,--wrap=calloc',
    '-Wl,--wrap=realloc',
    '-Wl,--wrap=free'
)

$Sources = @(
    (Join-Path $Root 'bootstrap/main.c'),
    (Join-Path $Root 'bootstrap/version.c'),
    (Join-Path $Root 'bootstrap/lexer.c'),
    (Join-Path $Root 'bootstrap/parser.c'),
    (Join-Path $Root 'bootstrap/interp.c'),
    (Join-Path $Root 'bootstrap/diagnostics.c'),
    (Join-Path $Root 'bootstrap/driver.c'),
    (Join-Path $Root 'tools/leakcheck.c')
)

$Exe = Join-Path $OutDir 'siyana-leak.exe'
Write-Host "building $Exe with $CC"
& $CC @CFlags '-I' (Join-Path $Root 'bootstrap/include') @Sources @Wrap '-o' $Exe
if ($LASTEXITCODE -ne 0) { throw "leakcheck build failed (exit $LASTEXITCODE)" }

# Each case is a loop that builds one value of the given kind per iteration.
# Two iteration counts differ only in how many values were built, so the
# difference divided by the extra iterations is the leak per value. Source
# length differs between the two, which shifts the AST size -- so the baseline
# case exists to cancel that out, and the real figure is (N - baseline) - (M -
# baseline) over (N - M).
$Templates = [ordered]@{
    'nothing'        = 'Variable n: Number = 0; Loop n < {N} { n = n + 1; }'
    'struct'         = 'Struct P { x: Number, y: Number } Variable i: Number = 0; Loop i < {N} { Variable p: P = P { x: 1, y: 2 }; i = i + 1; }'
    'array'          = 'Variable i: Number = 0; Loop i < {N} { Variable a: [4]Word = [1, 2, 3, 4]; i = i + 1; }'
    'nested'         = 'Struct In { x: Number } Struct Out { a: In, b: Number } Variable i: Number = 0; Loop i < {N} { Variable o: Out = Out { a: In { x: 1 }, b: 2 }; i = i + 1; }'
    'string'         = 'Variable i: Number = 0; Loop i < {N} { Variable s: String = "abcdefghij"; i = i + 1; }'
    'concat'         = 'Variable i: Number = 0; Loop i < {N} { Variable s: String = "abcdefghij" + "x"; i = i + 1; }'

    # Narrowing cases. Each isolates one step of the struct path, so a leak can
    # be attributed rather than merely observed.
    'loop-only'      = 'Struct P { x: Number, y: Number } Variable i: Number = 0; Loop i < {N} { i = i + 1; }'
    'lit-discarded'  = 'Struct P { x: Number, y: Number } Variable i: Number = 0; Loop i < {N} { P { x: 1, y: 2 }; i = i + 1; }'
    'lit-printed'    = 'Struct P { x: Number, y: Number } Variable i: Number = 0; Loop i < {N} { Terminal.Write(P { x: 1, y: 2 }); i = i + 1; }'
    'field-read'     = 'Struct P { x: Number, y: Number } Variable p: P = P { x: 1, y: 2 }; Variable i: Number = 0; Loop i < {N} { Terminal.Write(p.x); i = i + 1; }'
    'reassign'       = 'Struct P { x: Number, y: Number } Variable p: P = P { x: 0, y: 0 }; Variable i: Number = 0; Loop i < {N} { p = P { x: 1, y: 2 }; i = i + 1; }'
    'field-assign'   = 'Struct P { x: Number, y: Number } Variable p: P = P { x: 0, y: 0 }; Variable i: Number = 0; Loop i < {N} { p.x = i; i = i + 1; }'
    'call-arg'       = 'Struct P { x: Number, y: Number } Function Take(v: P) -> Number { Return v.x; } Variable i: Number = 0; Loop i < {N} { Take(P { x: 1, y: 2 }); i = i + 1; }'
    'call-ret'       = 'Struct P { x: Number, y: Number } Function Make() -> P { Return P { x: 1, y: 2 }; } Variable i: Number = 0; Loop i < {N} { Variable q: P = Make(); i = i + 1; }'
    'elem-assign'    = 'Variable a: [4]Word = [0, 0, 0, 0]; Variable i: Number = 0; Loop i < {N} { a[0] = i; i = i + 1; }'
    'union-match'    = 'Union U { A, B(Number) } Variable i: Number = 0; Loop i < {N} { Variable u: U = U.A; Match u { U.A { } } i = i + 1; }'
}

function Get-Live([string]$Source) {
    $file = Join-Path $OutDir 'case.sl'
    [System.IO.File]::WriteAllText($file, $Source)
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Exe
    $psi.Arguments = "run `"$file`""
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $null = $p.StandardOutput.ReadToEnd()
    $err = $p.StandardError.ReadToEnd()
    $p.WaitForExit()
    $code = $p.ExitCode
    $p.Dispose()

    # A case that fails to run executes zero iterations and would report a
    # clean 0/iter -- the most dangerous possible false negative, because it
    # reads as evidence the leak is fixed. Refused rather than reported.
    if ($code -ne 0) {
        return [pscustomobject]@{ Blocks = -1; Bytes = -1; Error = $err.Trim() }
    }
    if ($err -match 'live blocks (\d+), bytes (\d+)') {
        return [pscustomobject]@{ Blocks = [int]$Matches[1]; Bytes = [int]$Matches[2]; Error = '' }
    }
    return [pscustomobject]@{ Blocks = -1; Bytes = -1; Error = "no leakcheck line; stderr: $err" }
}

Write-Host ''
if (-not $Quiet) {
    $header = '{0,-10} {1,12} {2,12} {3,14} {4,14} {5,14}' -f 'case', 'blocks@10', 'blocks@1010', 'blocks/iter', 'bytes/iter', 'note'
    Write-Host $header
    Write-Host ('-' * 72)
}

$leaking = @()

foreach ($name in $Templates.Keys) {
    if ($Only.Count -gt 0 -and $name -notin $Only) { continue }
    $template = $Templates[$name]

    $a = Get-Live ($template -replace '\{N\}', '10')
    $b = Get-Live ($template -replace '\{N\}', '1010')
    if ($null -eq $a -or $null -eq $b) {
        Write-Host ('{0,-10} measurement failed' -f $name)
        $leaking += $name
        continue
    }
    if ($a.Blocks -lt 0 -or $b.Blocks -lt 0) {
        # The case did not run. Reporting it as anything else would be a false
        # reading, so the actual diagnostic is shown instead.
        $first = ($a.Error -split "`r?`n" | Where-Object { $_ -match 'error' } | Select-Object -First 1)
        if (-not $first) { $first = $a.Error }
        $line = '{0,-10} DID NOT RUN: {1}' -f $name, $first
        Write-Host $line
        $leaking += $name
        continue
    }

    $iterations = 1000
    $perBlock = ($b.Blocks - $a.Blocks) / $iterations
    $perByte = ($b.Bytes - $a.Bytes) / $iterations
    if ($perBlock -ne 0 -or $perByte -ne 0) { $leaking += $name }

    if ($perBlock -eq 0 -and $perByte -eq 0) {
        if (-not $Quiet) {
            $line = '{0,-10} {1,12} {2,12} {3,14} {4,14} {5,14}' -f $name, $a.Blocks, $b.Blocks, 0, 0, 'clean'
            Write-Host $line
        }
    } else {
        $line = '{0,-10} {1,12} {2,12} {3,14} {4,14} {5,14}' -f $name, $a.Blocks, $b.Blocks, $perBlock, $perByte, 'LEAKING'
        Write-Host $line
    }
}

if ($leaking.Count -gt 0) {
    Write-Host ''
    Write-Host ("{0} case(s) leak: {1}" -f $leaking.Count, ($leaking -join ', ')) -ForegroundColor Red
    exit 1
}
if (-not $Quiet) {
    Write-Host ''
    Write-Host ("all {0} leak cases clean" -f $Templates.Count)
}
exit 0