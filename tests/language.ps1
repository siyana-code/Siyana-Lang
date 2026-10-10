<#
.SYNOPSIS
    Language tests for the Siyana Lang interpreter.

.DESCRIPTION
    Each case runs a small program and checks its output or its diagnostics.
    These are the Phase 2 exit criteria from docs/architecture.md:

      - siyana run hello.sy prints output
      - a failing parse reports line and column
      - a runtime type error names the offending expression

    Written as data rather than code so adding a case is one line, and so the
    full language slice is visible in one place when reviewing coverage.

.EXAMPLE
    ./tools/build.ps1 test
#>

param(
    [string]$ExePath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build/siyana.exe')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not (Test-Path $ExePath)) {
    Write-Error "Not built: $ExePath. Run: ./tools/build.ps1"
    exit 1
}

$script:ExePath = $ExePath
$script:Failures = 0
$script:Total = 0
$script:TempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("siyana-tests-" + [guid]::NewGuid())
New-Item -ItemType Directory -Force -Path $script:TempDir | Out-Null

function Invoke-Si([string[]]$SiArgs) {
    <#
      Runs the binary and captures stdout and stderr together.

      PowerShell wraps native stderr in an ErrorRecord, which re-formats the
      text and inserts line breaks mid-message. Comparing against that is
      fragile, so the raw streams are captured with .NET instead: the output
      stays byte-faithful and patterns can match it directly.
    #>
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $psi = [System.Diagnostics.ProcessStartInfo]::new()
        $psi.FileName = $script:ExePath
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        # `Arguments` rather than `ArgumentList`: the latter is .NET Core only,
        # and this must also run on Windows PowerShell 5.1.
        $psi.Arguments = ($SiArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '

        $process = [System.Diagnostics.Process]::Start($psi)
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()

        $code = $process.ExitCode
        # stderr first: diagnostics go there, and putting them first keeps them
        # adjacent in the captured text.
        $out = ($stderr + $stdout)
    } finally {
        $ErrorActionPreference = $previous
    }
    return @{ Code = $code; Out = $out }
}

function Test-Case([string]$Name, [scriptblock]$Body) {
    $script:Total++
    try {
        & $Body
        Write-Host "  PASS  $Name"
    } catch {
        Write-Host "  FAIL  $Name"
        Write-Host "        $_" -ForegroundColor DarkGray
        $script:Failures++
    }
}

# Runs a program and asserts it succeeds and prints exactly the expected lines.
function Test-Output([string]$Source, [string[]]$Expected) {
    $file = Join-Path $script:TempDir ("case-{0}.sy" -f [guid]::NewGuid())
    $Source | Out-File -Encoding ascii $file
    try {
        $r = Invoke-Si @('run', $file)
        if ($r.Code -ne 0) { throw "expected success, got exit $($r.Code): $($r.Out)" }
        $actual = @($r.Out -split "`r?`n" | Where-Object { $_ -ne '' })
        if ($actual.Count -ne $Expected.Count) {
            throw "expected $($Expected.Count) line(s) [$($Expected -join ', ')], got $($actual.Count) [$($actual -join ', ')]"
        }
        for ($i = 0; $i -lt $Expected.Count; $i++) {
            if ($actual[$i] -ne $Expected[$i]) {
                throw "line $($i + 1): expected '$($Expected[$i])', got '$($actual[$i])'"
            }
        }
    } finally {
        Remove-Item $file -ErrorAction SilentlyContinue
    }
}

# Like Test-Output but counts an empty output line as a real line.
#
# The standard helper filters blank lines, which is right for nearly every case
# but wrong for print(""): that prints one empty line, and filtering it would
# make the test pass for the wrong reason.
function Test-OutputRaw([string]$Source, [string[]]$Expected) {
    $file = Join-Path $script:TempDir ("case-{0}.sy" -f [guid]::NewGuid())
    $Source | Out-File -Encoding ascii $file
    try {
        $r = Invoke-Si @('run', $file)
        if ($r.Code -ne 0) { throw "expected success, got exit $($r.Code): $($r.Out)" }
        $lines = @($r.Out -split "`r?`n")
        # PowerShell keeps a trailing empty entry from the final newline.
        if ($lines.Count -gt 0 -and $lines[-1] -eq '') { $lines = $lines[0..($lines.Count - 2)] }
        if ($lines.Count -ne $Expected.Count) {
            throw "expected $($Expected.Count) line(s) [$($Expected -join ', ')], got $($lines.Count) [$($lines -join ', ')]"
        }
        for ($i = 0; $i -lt $Expected.Count; $i++) {
            if ($lines[$i] -ne $Expected[$i]) {
                throw "line $($i + 1): expected '$($Expected[$i])', got '$($lines[$i])'"
            }
        }
    } finally {
        Remove-Item $file -ErrorAction SilentlyContinue
    }
}

# Runs a program and asserts it fails with a diagnostic matching the pattern.
function Test-Diagnostic([string]$Source, [string]$Pattern) {
    $file = Join-Path $script:TempDir ("case-{0}.sy" -f [guid]::NewGuid())
    $Source | Out-File -Encoding ascii $file
    try {
        $r = Invoke-Si @('run', $file)
        if ($r.Code -eq 0) { throw "expected failure, got success: $($r.Out)" }
        if ($r.Out -notmatch $Pattern) {
            throw "diagnostic did not match /$Pattern/: $($r.Out.Trim())"
        }
    } finally {
        Remove-Item $file -ErrorAction SilentlyContinue
    }
}

Write-Host 'running language tests'

# --- Literals and printing --------------------------------------------------

Test-Case 'prints a string'          { Test-Output 'print("hello");' @('hello') }
Test-Case 'prints an integer'        { Test-Output 'print(42);' @('42') }
Test-Case 'prints a negative integer' { Test-Output 'print(-7);' @('-7') }
Test-Case 'prints true and false'    { Test-Output 'print(true); print(false);' @('true', 'false') }
Test-Case 'prints an empty string'   { Test-OutputRaw 'print("");' @('') }

# --- Arithmetic -------------------------------------------------------------

Test-Case 'addition'        { Test-Output 'print(2 + 3);' @('5') }
Test-Case 'subtraction'     { Test-Output 'print(9 - 4);' @('5') }
Test-Case 'multiplication'  { Test-Output 'print(6 * 7);' @('42') }
Test-Case 'division'        { Test-Output 'print(20 / 4);' @('5') }
Test-Case 'unary negation'  { Test-Output 'print(-(3 + 4));' @('-7') }
Test-Case 'precedence: * before +'   { Test-Output 'print(2 + 3 * 4);' @('14') }
Test-Case 'precedence: parens'       { Test-Output 'print((2 + 3) * 4);' @('20') }
Test-Case 'left associativity'       { Test-Output 'print(10 - 3 - 2);' @('5') }
Test-Case 'division by zero is an error' {
    Test-Diagnostic 'print(1 / 0);' 'division by zero'
}

# --- Comparison and logic ---------------------------------------------------

Test-Case 'equality'          { Test-Output 'print(3 == 3);' @('true') }
Test-Case 'inequality'        { Test-Output 'print(3 != 3);' @('false') }
Test-Case 'less than'         { Test-Output 'print(2 < 5);' @('true') }
Test-Case 'less or equal'     { Test-Output 'print(5 <= 5);' @('true') }
Test-Case 'greater than'      { Test-Output 'print(9 > 2);' @('true') }
Test-Case 'greater or equal'  { Test-Output 'print(2 >= 9);' @('false') }
Test-Case 'logical not'       { Test-Output 'print(!false);' @('true') }
Test-Case 'double negation'   { Test-Output 'print(!!true);' @('true') }
Test-Case 'string equality compares contents' {
    Test-Output 'print("ab" == "ab"); print("ab" == "ac");' @('true', 'false')
}

# --- Variables --------------------------------------------------------------

Test-Case 'declare and print' {
    Test-Output 'var x: int = 5; print(x);' @('5')
}
Test-Case 'reassignment' {
    Test-Output 'var x: int = 5; x = 9; print(x);' @('9')
}
Test-Case 'undeclared variable is an error' {
    Test-Diagnostic 'print(nope);' "undefined variable 'nope'"
}
Test-Case 'type mismatch on initialiser is an error' {
    Test-Diagnostic 'var x: int = true;' 'cannot initialise'
}
Test-Case 'duplicate declaration is an error' {
    Test-Diagnostic 'var x: int = 1; var x: int = 2;' 'already declared'
}

# --- Control flow -----------------------------------------------------------

Test-Case 'if takes the true branch' {
    Test-Output 'if (1 < 2) { print(1); } else { print(2); }' @('1')
}
Test-Case 'if takes the else branch' {
    Test-Output 'if (2 < 1) { print(1); } else { print(2); }' @('2')
}
Test-Case 'if without else and a false condition prints nothing' {
    Test-Output 'if (false) { print(1); }' @()
}
Test-Case 'while loop' {
    Test-Output 'var i: int = 0; while (i < 3) { print(i); i = i + 1; }' @('0', '1', '2')
}
Test-Case 'non-bool condition is an error' {
    Test-Diagnostic 'if (1) { print(1); }' 'condition of ''if'' must be bool'
}

# --- Functions --------------------------------------------------------------

Test-Case 'function call with no arguments' {
    Test-Output 'fn greet() { print("hi"); } greet();' @('hi')
}
Test-Case 'function with a parameter' {
    Test-Output 'fn show(a: int) { print(a); } show(7);' @('7')
}
Test-Case 'function with two parameters' {
    Test-Output 'fn add(a: int, b: int) { print(a + b); } add(2, 3);' @('5')
}
Test-Case 'recursive factorial' {
    Test-Output 'fn fact(n: int) { if (n <= 1) { return 1; } return n * fact(n - 1); } print(fact(5));' @('120')
}
Test-Case 'functions may be declared after use' {
    Test-Output 'main_helper(); fn main_helper() { print("later"); }' @('later')
}
Test-Case 'undefined function is an error' {
    Test-Diagnostic 'nothere();' "undefined function 'nothere'"
}
Test-Case 'wrong argument count is an error' {
    Test-Diagnostic 'fn f(a: int) { } f(1, 2);' 'expects 1 argument'
}
Test-Case 'wrong argument type is an error' {
    Test-Diagnostic 'fn f(a: int) { } f(true);' "parameter 'a'.*expects int"
}

# --- Comments ---------------------------------------------------------------

Test-Case 'line comments are ignored' {
    Test-Output "// a comment`nprint(1); // trailing" @('1')
}
Test-Case 'block comments are ignored' {
    Test-Output "/* a`n   block comment */`nprint(1);" @('1')
}

# --- Diagnostics: parse errors report line and column ------------------------

Test-Case 'missing semicolon reports line and column' {
    # Out-File appends a trailing newline, so the failure surfaces as
    # "found end of file" on line 2. The position is still tracked, which is
    # what this asserts.
    Test-Diagnostic 'print(1)' '\.sy:2:1: error: expected'
}
# Assigned to a variable first rather than passed as an inline literal: a `{`
# inside a scriptblock argument confuses how PowerShell parses the enclosing
# brace, so the case name was being written to the source file instead.
$unclosedBlock = 'if (true) { print(1);'
Test-Case 'unclosed brace is reported' {
    Test-Diagnostic $script:unclosedBlock 'unclosed'
}
# Source fragments containing braces or quotes are held in variables first.
# Passed inline they confuse how PowerShell parses the enclosing scriptblock,
# and the case name ends up in the source file instead of the program.
$unterminatedString = 'print("abc);'
$unterminatedComment = '/* nope'
$unexpectedCharacter = 'print(#);'

Test-Case 'unterminated string is reported' {
    Test-Diagnostic $script:unterminatedString 'unterminated string'
}
Test-Case 'unterminated block comment is reported' {
    Test-Diagnostic $script:unterminatedComment 'unterminated block comment'
}
Test-Case 'unexpected character is reported' {
    Test-Diagnostic $script:unexpectedCharacter 'unexpected character'
}

# --- Diagnostics: the error names the offending expression -------------------

$compareMismatch = 'print(1 == "a");'
$badOperand = 'print(true + 1);'
$notNonBool = 'print(!1);'

Test-Case 'mismatched comparison names both types' {
    Test-Diagnostic $script:compareMismatch 'cannot compare int with string'
}
Test-Case 'bad operand type names the operator' {
    Test-Diagnostic $script:badOperand 'requires int operands'
}
Test-Case 'non-bool operand to ! is reported' {
    Test-Diagnostic $script:notNonBool 'requires a bool'
}

# --- Report -----------------------------------------------------------------

Write-Host ''
Remove-Item -Recurse -Force $script:TempDir -ErrorAction SilentlyContinue

if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) of $($script:Total) test(s) failed" -ForegroundColor Red
    exit 1
}
Write-Host "all $($script:Total) tests passed"

# Exit explicitly: $LASTEXITCODE still holds the value from the last native
# command run, and several cases above run programs that fail on purpose.
exit 0