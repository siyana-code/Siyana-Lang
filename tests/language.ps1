<#
.SYNOPSIS
    Language tests for the Siyana Lang interpreter.

.DESCRIPTION
    Each case runs a small program and checks its output or its diagnostics.

    Covers the Phase 3 language: Number and Character as the only primitives,
    full-name keywords, Terminal as a value with Write and ReadLine, and
    C-style truthiness per decision 17.

    Written as data where possible so adding a case is one line, and so the
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

function Invoke-Si([string[]]$SiArgs, [string]$Stdin = '') {
    <#
      Runs the binary and captures stdout and stderr together.

      PowerShell wraps native stderr in an ErrorRecord, which re-formats the
      text and inserts line breaks mid-message. Comparing against that is
      fragile, so the raw streams are captured with .NET instead.

      `Arguments` is used rather than `ArgumentList`, which is .NET Core only;
      this must also run on Windows PowerShell 5.1.
    #>
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $script:ExePath
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    # Redirected so stdin can be supplied and then closed. Without this,
    # StandardInput is null and a program reading input would hang on a
    # terminal rather than seeing end of input.
    $psi.RedirectStandardInput = $true
    $psi.Arguments = ($SiArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '

    $process = [System.Diagnostics.Process]::Start($psi)
    if ($Stdin) { $process.StandardInput.Write($Stdin) }
    $process.StandardInput.Close()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    # stderr first: diagnostics go there, and keeping them adjacent in the
    # captured text makes failures easier to read.
    return @{ Code = $process.ExitCode; Out = ($stderr + $stdout) }
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
function Test-Output([string]$Source, [string[]]$Expected, [string]$Stdin = '') {
    $file = Join-Path $script:TempDir ("case-{0}.sl" -f [guid]::NewGuid())
    $Source | Out-File -Encoding ascii $file
    try {
        $r = Invoke-Si @('run', $file) $Stdin
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

# Like Test-Output but counts an empty output line as a real line. Test-Output
# filters blanks, which is right almost everywhere but wrong for a program
# whose only output is an empty line -- it would pass for the wrong reason.
function Test-OutputRaw([string]$Source, [string[]]$Expected) {
    $file = Join-Path $script:TempDir ("case-{0}.sl" -f [guid]::NewGuid())
    $Source | Out-File -Encoding ascii $file
    try {
        $r = Invoke-Si @('run', $file)
        if ($r.Code -ne 0) { throw "expected success, got exit $($r.Code): $($r.Out)" }
        $lines = @($r.Out -split "`r?`n")
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
    $file = Join-Path $script:TempDir ("case-{0}.sl" -f [guid]::NewGuid())
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

# --- Terminal ---------------------------------------------------------------

Test-Case 'Terminal.Write prints a string' { Test-Output 'Terminal.Write("hello");' @('hello') }
Test-Case 'Terminal.Write prints a Number' { Test-Output 'Terminal.Write(42);' @('42') }
Test-Case 'Terminal.Write appends a newline' { Test-OutputRaw 'Terminal.Write("x");' @('x') }
Test-Case 'Terminal.Write prints True and False' {
    Test-Output 'Terminal.Write(True); Terminal.Write(False);' @('True', 'False')
}
Test-Case 'Terminal.Write an empty string' { Test-OutputRaw 'Terminal.Write("");' @('') }
Test-Case 'Terminal.ReadLine reads a line' {
    Test-Output 'Variable line: Number = 0; Terminal.Write(1);' @('1') "typed input`n"
}

# --- Literals and types -----------------------------------------------------

Test-Case 'Number literal'            { Test-Output 'Terminal.Write(7);' @('7') }
Test-Case 'negative Number'           { Test-Output 'Terminal.Write(-7);' @('-7') }
Test-Case 'Character literal'         { Test-Output "Terminal.Write('A');" @('A') }
Test-Case 'Character escape'          { Test-Output "Terminal.Write('\n');" @() }
Test-Case 'String literal'            { Test-Output 'Terminal.Write("text");' @('text') }

# --- Arithmetic -------------------------------------------------------------

Test-Case 'addition'       { Test-Output 'Terminal.Write(2 + 3);' @('5') }
Test-Case 'subtraction'    { Test-Output 'Terminal.Write(9 - 4);' @('5') }
Test-Case 'multiplication' { Test-Output 'Terminal.Write(6 * 7);' @('42') }
Test-Case 'division'       { Test-Output 'Terminal.Write(20 / 4);' @('5') }
Test-Case 'unary negation' { Test-Output 'Terminal.Write(-(3 + 4));' @('-7') }
Test-Case 'precedence: * before +' { Test-Output 'Terminal.Write(2 + 3 * 4);' @('14') }
Test-Case 'precedence: parens'     { Test-Output 'Terminal.Write((2 + 3) * 4);' @('20') }
Test-Case 'left associativity'     { Test-Output 'Terminal.Write(10 - 3 - 2);' @('5') }
Test-Case 'division by zero is an error' {
    Test-Diagnostic 'Terminal.Write(1 / 0);' 'division by zero'
}

# --- Comparison -------------------------------------------------------------

Test-Case 'equality'    { Test-Output 'Terminal.Write(3 == 3);' @('True') }
Test-Case 'inequality'  { Test-Output 'Terminal.Write(3 != 3);' @('False') }
Test-Case 'less than'   { Test-Output 'Terminal.Write(2 < 5);' @('True') }
Test-Case 'less equal'  { Test-Output 'Terminal.Write(5 <= 5);' @('True') }
Test-Case 'greater'     { Test-Output 'Terminal.Write(9 > 2);' @('True') }
Test-Case 'greater eq'  { Test-Output 'Terminal.Write(2 >= 9);' @('False') }
Test-Case 'Character equality' { Test-Output "Terminal.Write('a' == 'a');" @('True') }
Test-Case 'string equality compares contents' {
    Test-Output 'Terminal.Write("ab" == "ab"); Terminal.Write("ab" == "ac");' @('True', 'False')
}
Test-Case 'Number and Character never mix' {
    Test-Diagnostic "Terminal.Write(1 == 'a');" 'cannot compare'
}

# --- C-style truthiness (decision 17) ---------------------------------------

Test-Case 'not of True is False'  { Test-Output 'Terminal.Write(!True);' @('False') }
Test-Case 'not of False is True'  { Test-Output 'Terminal.Write(!False);' @('True') }
Test-Case 'If (1) is True'         { Test-Output 'If (1) { Terminal.Write("yes"); }' @('yes') }
Test-Case 'If (0) is False'        { Test-Output 'If (0) { Terminal.Write("yes"); } Else { Terminal.Write("no"); }' @('no') }
Test-Case 'If (42) is True'        { Test-Output 'If (42) { Terminal.Write("yes"); }' @('yes') }
Test-Case 'Loop (0) does not run'  { Test-Output 'Loop (0) { Terminal.Write("x"); }' @() }
Test-Case 'If on a String is an error' {
    Test-Diagnostic 'If ("x") { }' "condition of 'If' must be Number or Character"
}

# --- Variables --------------------------------------------------------------

Test-Case 'declare and print' {
    Test-Output 'Variable x: Number = 5; Terminal.Write(x);' @('5')
}
Test-Case 'declare a Character' {
    Test-Output "Variable c: Character = 'z'; Terminal.Write(c);" @('z')
}
Test-Case 'reassignment' {
    Test-Output 'Variable x: Number = 5; x = 9; Terminal.Write(x);' @('9')
}
Test-Case 'undefined variable is an error' {
    Test-Diagnostic 'Terminal.Write(nope);' "undefined variable 'nope'"
}
Test-Case 'Number cannot hold a Character' {
    Test-Diagnostic "Variable x: Number = 'a';" 'cannot initialise'
}
Test-Case 'Character cannot hold a Number' {
    Test-Diagnostic 'Variable c: Character = 65;' 'cannot initialise'
}
Test-Case 'duplicate declaration is an error' {
    Test-Diagnostic 'Variable x: Number = 1; Variable x: Number = 2;' 'already declared'
}

# --- Control flow -----------------------------------------------------------

Test-Case 'If takes the true branch' {
    Test-Output 'If (1 < 2) { Terminal.Write(1); } Else { Terminal.Write(2); }' @('1')
}
Test-Case 'If takes the else branch' {
    Test-Output 'If (2 < 1) { Terminal.Write(1); } Else { Terminal.Write(2); }' @('2')
}
Test-Case 'If without Else and false prints nothing' {
    Test-Output 'If (0) { Terminal.Write(1); }' @()
}
Test-Case 'Else If chain picks the middle' {
    Test-Output 'If (0) { Terminal.Write(1); } Else If (1) { Terminal.Write(2); } Else { Terminal.Write(3); }' @('2')
}
Test-Case 'Else If chain falls through to Else' {
    Test-Output 'If (0) { Terminal.Write(1); } Else If (0) { Terminal.Write(2); } Else { Terminal.Write(3); }' @('3')
}
Test-Case 'Loop iterates' {
    Test-Output 'Variable i: Number = 0; Loop (i < 3) { Terminal.Write(i); i = i + 1; }' @('0', '1', '2')
}
Test-Case 'nested Loop' {
    Test-Output @'
Variable i: Number = 0;
Loop (i < 2) {
    Variable j: Number = 0;
    Loop (j < 2) { Terminal.Write(i * 2 + j); j = j + 1; }
    i = i + 1;
}
'@ @('0', '1', '2', '3')
}

# --- Functions --------------------------------------------------------------

Test-Case 'Function with no arguments' {
    Test-Output 'Function Greet() { Terminal.Write("hi"); } Greet();' @('hi')
}
Test-Case 'Function with a parameter' {
    Test-Output 'Function Show(a: Number) { Terminal.Write(a); } Show(7);' @('7')
}
Test-Case 'Function with two parameters' {
    Test-Output 'Function Add(a: Number, b: Number) { Terminal.Write(a + b); } Add(2, 3);' @('5')
}
Test-Case 'Function with a Character parameter' {
    Test-Output "Function Show(a: Character) { Terminal.Write(a); } Show('q');" @('q')
}
Test-Case 'Function with a declared return type' {
    Test-Output 'Function Double(a: Number) -> Number { Return a * 2; } Terminal.Write(Double(4));' @('8')
}
Test-Case 'recursion' {
    Test-Output 'Function Fact(n: Number) -> Number { If (n <= 1) { Return 1; } Return n * Fact(n - 1); } Terminal.Write(Fact(5));' @('120')
}
Test-Case 'Function may be declared after use' {
    Test-Output 'Later(); Function Later() { Terminal.Write("later"); }' @('later')
}
Test-Case 'undefined function is an error' {
    Test-Diagnostic 'Nope();' "undefined function 'Nope'"
}
Test-Case 'wrong argument count is an error' {
    Test-Diagnostic 'Function F(a: Number) { } F(1, 2);' 'expects 1 argument'
}
Test-Case 'True is acceptable where Number is expected' {
    # Expected, not a bug: True is Number 1 (decision 17), so passing it to a
    # Number parameter is correct. This test exists so that the consequence is
    # pinned down rather than discovered later as a surprise.
    Test-Output 'Function F(a: Number) { Terminal.Write(a); } F(True);' @('1')
}
Test-Case 'wrong argument type is an error' {
    # Character is the distinct primitive, so passing one to a Number
    # parameter must fail.
    Test-Diagnostic "Function F(a: Number) { } F('c');" "parameter 'a'.*expects Number"
}
Test-Case 'unknown type is an error' {
    Test-Diagnostic 'Variable x: Widget = 1;' "expected type 'Number' or 'Character'"
}

# --- Comments ---------------------------------------------------------------

Test-Case 'line comments are ignored' {
    Test-Output "// a comment`nTerminal.Write(1); // trailing" @('1')
}
Test-Case 'block comments are ignored' {
    Test-Output "/* a`n   block comment */`nTerminal.Write(1);" @('1')
}

# --- Diagnostics ------------------------------------------------------------

# Source fragments containing braces or quotes are held in variables first.
# Passed inline they confuse how PowerShell parses the enclosing scriptblock,
# and the case name ends up in the source file instead of the program.
$unclosedBlock = 'If (True) { Terminal.Write(1);'
$unterminatedString = 'Terminal.Write("abc);'
$unterminatedComment = '/* nope'
$unexpectedCharacter = 'Terminal.Write(#);'
$unknownMethod = 'Terminal.Frob(1);'
$emptyCharacter = "Terminal.Write('');"
$longCharacter = "Terminal.Write('ab');"

Test-Case 'missing semicolon reports line and column' {
    Test-Diagnostic 'Terminal.Write(1)' '\.sl:2:1: error: expected'
}
Test-Case 'unclosed brace is reported' {
    Test-Diagnostic $script:unclosedBlock 'unclosed'
}
Test-Case 'unterminated string is reported' {
    Test-Diagnostic $script:unterminatedString 'unterminated string'
}
Test-Case 'unterminated block comment is reported' {
    Test-Diagnostic $script:unterminatedComment 'unterminated block comment'
}
Test-Case 'unexpected character is reported' {
    Test-Diagnostic $script:unexpectedCharacter 'unexpected character'
}
Test-Case 'unknown Terminal method is reported' {
    Test-Diagnostic $script:unknownMethod "expected 'Write' or 'ReadLine'"
}
Test-Case 'empty character literal is reported' {
    Test-Diagnostic $script:emptyCharacter 'empty character literal'
}
Test-Case 'multi-byte character literal is rejected' {
    Test-Diagnostic $script:longCharacter 'more than one byte'
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