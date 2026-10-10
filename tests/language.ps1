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
    Test-Diagnostic 'If ("x") { }' "condition of 'If' must be an integer"
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
Test-Case 'Number cannot hold a Character literal' {
    # A Character *literal* cannot become a Number: narrowing is only from a
    # Number literal, never across the byte boundary.
    Test-Diagnostic "Variable x: Number = 'a';" 'cannot initialise'
}
Test-Case 'a Number literal narrows to Character when it fits' {
    Test-Output 'Variable c: Character = 65; Terminal.Write(c);' @('A')
}
Test-Case 'a Number literal does not narrow past Character' {
    Test-Diagnostic 'Variable c: Character = 300;' 'cannot initialise'
}
Test-Case 'a Number literal narrows to Number32 when it fits' {
    Test-Output 'Variable n: Number32 = 5; Terminal.Write(n);' @('5')
}
Test-Case 'a Number literal does not narrow to Number32 when too large' {
    Test-Diagnostic 'Variable n: Number32 = 5000000000;' 'cannot initialise'
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
    # A named type resolves at run time, so an undeclared one is reported when
    # the declaration is reached rather than at parse time.
    Test-Diagnostic 'Variable x: Widget = 1;' "undefined type 'Widget'"
}

# --- Integer widths ---------------------------------------------------------

Test-Case 'Number32 holds 32-bit values' {
    Test-Output 'Variable n: Number32 = 2147483647; Terminal.Write(n);' @('2147483647')
}
Test-Case 'Word holds large sizes' {
    Test-Output 'Variable w: Word = 4294967295; Terminal.Write(w);' @('4294967295')
}
Test-Case 'Word is unsigned, so it cannot go negative' {
    Test-Diagnostic 'Variable w: Word = 0; w = w - 1;' 'overflow'
}
Test-Case 'a negative literal cannot adopt Word' {
    # The protection that matters: a positive literal is unambiguously that
    # value whatever type it becomes, but a negative one must never be
    # reinterpreted as a huge unsigned number.
    Test-Diagnostic 'Variable w: Word = 0; w = w + -1;' 'same type'
}
Test-Case 'negating a Word is an error' {
    Test-Diagnostic 'Variable w: Word = 1; Terminal.Write(-w);' 'no negative values'
}
Test-Case 'Number32 overflow is an error, not a wrap' {
    # The critical guarantee. C would silently produce -2147483648 here.
    Test-Diagnostic 'Variable n: Number32 = 2147483647; Variable m: Number32 = n + 1;' 'overflow'
}
Test-Case 'Number32 underflow is an error' {
    Test-Diagnostic 'Variable n: Number32 = -2147483648; Variable m: Number32 = n - 1;' 'overflow'
}
Test-Case 'Number overflow is an error' {
    Test-Diagnostic 'Variable n: Number = 9223372036854775807; Variable m: Number = n + 1;' 'overflow'
}
Test-Case 'Character overflow is an error, not a wrap to zero' {
    Test-Diagnostic 'Variable c: Character = 255; Variable d: Character = c + 1;' 'overflow'
}
Test-Case 'mixed widths cannot be added' {
    Test-Diagnostic 'Variable a: Number32 = 1; Variable b: Number = 1; Variable c: Number = a + b;' 'same type'
}
Test-Case 'Number32 and Number cannot be compared' {
    Test-Diagnostic 'Variable a: Number32 = 1; Variable b: Number = 1; Terminal.Write(a == b);' 'cannot compare'
}
Test-Case 'mixed widths multiply is rejected' {
    Test-Diagnostic 'Variable c: Character = 1; Variable n: Number = 2; Variable r: Number = c * n;' 'same type'
}
Test-Case 'arithmetic yields the operand type' {
    Test-Output 'Variable a: Number32 = 3; Variable b: Number32 = 4; Variable c: Number32 = a * b; Terminal.Write(c);' @('12')
}
Test-Case 'Word arithmetic works' {
    Test-Output 'Variable w: Word = 100; Variable x: Word = w - 30; Terminal.Write(x);' @('70')
}
Test-Case 'Word comparison is unsigned' {
    # Signed comparison would make 0-1 appear less than any positive value.
    Test-Output 'Variable w: Word = 4294967295; Terminal.Write(w > 1);' @('True')
}

# --- Structs ----------------------------------------------------------------

Test-Case 'a struct field can be read' {
    $src = @'
Struct Point {
    x: Number32,
    y: Number32,
}
Variable p: Point = Point { x: 3, y: 4 };
Terminal.Write(p.x);
Terminal.Write(p.y);
'@
    Test-Output $src @('3', '4')
}
Test-Case 'field order in a literal does not matter' {
    Test-Output 'Struct P { a: Number32, b: Number32, } Variable p: P = P { b: 2, a: 1 }; Terminal.Write(p);' @('P {a: 1, b: 2}')
}
Test-Case 'a struct field can be assigned' {
    Test-Output 'Struct P { a: Number32, } Variable p: P = P { a: 1 }; p.a = 9; Terminal.Write(p.a);' @('9')
}
Test-Case 'a struct copies on assignment' {
    # The aliasing bug this guards: `b` must not share `a`'s storage, or writing
    # through one variable silently changes the other.
    $src = @'
Struct P { a: Number32, }
Variable a: P = P { a: 1 };
Variable b: P = a;
b.a = 99;
Terminal.Write(a.a);
Terminal.Write(b.a);
'@
    Test-Output $src @('1', '99')
}
Test-Case 'a struct passes to a function by value' {
    $src = @'
Struct P { a: Number32, }
Function Bump(p: P) {
    p.a = 42;
}
Variable p: P = P { a: 1 };
Bump(p);
Terminal.Write(p.a);
'@
    Test-Output $src @('1')
}
Test-Case 'a literal must set every field' {
    # Nothing-as-placeholder hides the bug until much later, so a partial
    # literal is rejected outright.
    Test-Diagnostic 'Struct P { a: Number32, b: Number32, } Variable p: P = P { a: 1 };' "missing a value for field 'b'"
}
Test-Case 'an unknown field is an error' {
    Test-Diagnostic 'Struct P { a: Number32, } Variable p: P = P { a: 1, c: 2 };' "has no field 'c'"
}
Test-Case 'setting a field twice is an error' {
    Test-Diagnostic 'Struct P { a: Number32, } Variable p: P = P { a: 1, a: 2 };' "field 'a' is set twice"
}
Test-Case 'a field keeps its declared type' {
    Test-Diagnostic 'Struct P { a: Word, } Variable p: P = P { a: -1 };' "field 'a' of 'P' expects Word"
}
Test-Case 'a duplicate struct name is an error' {
    Test-Diagnostic 'Struct P { a: Number32, } Struct P { b: Number32, }' "already declared"
}
Test-Case 'a duplicate field name is an error' {
    Test-Diagnostic 'Struct P { a: Number32, a: Word, }' "declared twice"
}
Test-Case 'a struct may be declared after its use' {
    # Declarations are hoisted, so a type may be used above the line that
    # defines it -- the same rule functions already follow.
    Test-Output 'Variable p: Point = Point { x: 1 }; Struct Point { x: Number32, } Terminal.Write(p.x);' @('1')
}

# --- Tagged unions ----------------------------------------------------------

Test-Case 'a union holds one variant' {
    $src = @'
Union Value {
    Nothing,
    Number(Number),
}
Variable v: Value = Value.Number(42);
Terminal.Write(v);
'@
    Test-Output $src @('Value.Number(42)')
}
Test-Case 'a payload-less variant needs no argument' {
    Test-Output 'Union V { Nothing, } Variable v: V = V.Nothing; Terminal.Write(v);' @('V.Nothing')
}
Test-Case 'Match selects the active arm' {
    $src = @'
Union Value {
    Nothing,
    Number(Number),
    Flag(Number),
}
Function Describe(v: Value) {
    Match v {
        Value.Nothing { Terminal.Write("nothing"); }
        Value.Number(n) { Terminal.Write("number ", n); }
        Value.Flag(f) { Terminal.Write("flag ", f); }
    }
}
Describe(Value.Nothing);
Describe(Value.Number(7));
Describe(Value.Flag(1));
'@
    Test-Output $src @('nothing', 'number 7', 'flag 1')
}
Test-Case 'a union may be named after a type keyword' {
    # `Number(Number)` is the natural spelling, so the lexer must give type
    # keywords owned text and the parser must accept them as names.
    Test-Output 'Union V { Number(Number), } Variable v: V = V.Number(1); Terminal.Write(v);' @('V.Number(1)')
}
Test-Case 'an unhandled variant is an error, not a fall-through' {
    # Exhaustiveness checked when the Match is reached, so a value built
    # dynamically still cannot escape.
    $src = @'
Union V {
    A,
    B,
}
Variable v: V = V.B;
Match v {
    V.A { Terminal.Write("a"); }
}
'@
    Test-Diagnostic $src "not exhaustive"
}
Test-Case 'Match needs a union' {
    Test-Diagnostic 'Union V { A, } Match 1 { V.A { } }' 'Match needs a union'
}
Test-Case 'Match arms must name one type' {
    Test-Diagnostic 'Union V { A, } Union W { B, } Variable v: V = V.A; Match v { V.A { } W.B { } }' 'must all name the same type'
}
Test-Case 'a union field cannot be read directly' {
    # The active variant decides what a field means, so a read would be a guess.
    Test-Diagnostic 'Union V { Number(Number), } Variable v: V = V.Number(1); Terminal.Write(v.Number);' 'use Match'
}
Test-Case 'a variant payload is type checked' {
    Test-Diagnostic 'Union V { Count(Word), } Variable v: V = V.Count(-1);' "variant 'Count' of 'V' expects Word"
}
Test-Case 'a payload-less variant rejects a payload' {
    Test-Diagnostic 'Union V { A, B(Number), } Variable v: V = V.A(1);' 'takes no payload'
}
Test-Case 'a payload variant requires a payload' {
    Test-Diagnostic 'Union V { A, B(Number), } Variable v: V = V.B;' 'needs a payload'
}
Test-Case 'an unknown variant is an error' {
    Test-Diagnostic 'Union V { A, } Variable v: V = V.Missing;' "has no variant 'Missing'"
}
Test-Case 'a struct has no variants' {
    Test-Diagnostic 'Struct P { a: Number32, } Variable v: P = P.A;' "is a struct"
}
Test-Case 'a union is not built with a struct literal' {
    Test-Diagnostic 'Union V { A, } Variable v: V = V { a: 1 };' "construct it with"
}
Test-Case 'a duplicate variant name is an error' {
    Test-Diagnostic 'Union V { A, A, }' "declared twice"
}
Test-Case 'an empty declaration is an error' {
    Test-Diagnostic 'Struct P { }' 'at least one field'
}

# --- Blocks are not struct literals ----------------------------------------
#
# `If (x) { }` puts an identifier directly in front of a brace, which is the
# same shape as `Type { field: value }`. The parser resolves this by context,
# so every place a brace follows an identifier needs a test -- an earlier
# version broke If and Loop while every existing test still passed, because
# they all used literal conditions.

Test-Case 'If on a named variable is not a struct literal' {
    Test-Output 'Variable one: Number = 1; If (one) { Terminal.Write("yes"); }' @('yes')
}
Test-Case 'Loop on a named variable is not a struct literal' {
    $src = @'
Variable n: Number = 3;
Variable total: Number = 0;
Loop (n) {
    total = total + 1;
    n = n - 1;
}
Terminal.Write(total);
'@
    Test-Output $src @('3')
}
Test-Case 'Else If on a named variable is not a struct literal' {
    $src = @'
Variable n: Number = 2;
If (n == 1) {
    Terminal.Write("one");
} Else If (n == 2) {
    Terminal.Write("two");
} Else {
    Terminal.Write("many");
}
'@
    Test-Output $src @('two')
}
Test-Case 'Match on a named variable is not a struct literal' {
    $src = @'
Union V { A, B, }
Variable v: V = V.B;
Match v {
    V.A { Terminal.Write("a"); }
    V.B { Terminal.Write("b"); }
}
'@
    Test-Output $src @('b')
}
Test-Case 'a struct literal still works in a call argument' {
    # The guard is scoped to the condition, not global: an argument position
    # has no block to confuse it with.
    Test-Output 'Struct P { a: Number32, } Function Show(p: P) { Terminal.Write(p.a); } Show(P { a: 7 });' @('7')
}
Test-Case 'a union construction still works in a call argument' {
    Test-Output 'Union V { A, B(Number), } Function Show(v: V) { Match v { V.A { Terminal.Write("a"); } V.B(n) { Terminal.Write(n); } } } Show(V.B(8));' @('8')
}

# --- Error declarations -----------------------------------------------------

Test-Case 'Error declares a payload-less union' {
    # Same machinery as Union, so Error sets can be declared before any
    # fallible function exists.
    Test-Output 'Error Io { NotFound, Denied, } Terminal.Write("declared");' @('declared')
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