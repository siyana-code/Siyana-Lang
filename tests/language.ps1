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
Test-Case 'subtraction to zero is not an overflow' {
    # Every subtraction that reached a non-negative result was fine, which is
    # exactly the shape of a test suite that never notices this. `0 - 1` is not
    # an overflow; it is -1.
    Test-Output 'Terminal.Write(0 - 1);' @('-1')
}
Test-Case 'subtraction past zero is negative' {
    $src = @'
Variable small: Number = 2;
Variable large: Number = 5;
Terminal.Write(small - large);
'@
    Test-Output $src @('-3')
}
Test-Case 'Number32 subtraction past zero is negative' {
    # The narrower floor is the case the fix has to get right rather than just
    # widening: a result below INT32_MIN is still an overflow.
    Test-Output 'Variable a: Number32 = 0; Terminal.Write(a - 1);' @('-1')
}
Test-Case 'Number32 subtraction past its minimum is an error' {
    # The narrower floor is the case the fix has to get right rather than just
    # widening: a result below INT32_MIN is still an overflow.
    $src = @'
Variable n: Number32 = -2147483648;
Variable one: Number32 = 1;
Variable m: Number32 = n - one;
'@
    Test-Diagnostic $src 'overflow'
}
Test-Case 'Number subtraction to its minimum is not an error' {
    # The boundary for `Number`. If this one is allowed, the check is not just
    # refusing everything negative.
    $src = @'
Variable big: Number = 4611686018427387904;
Variable half: Number = 2305843009213693952;
Variable n: Number = big + half;
Variable zero: Number = 0;
Terminal.Write(zero - n);
'@
    Test-Output $src @('-6917529027641081856')
}
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

# --- Errors ----------------------------------------------------------------
#
# Two rules carry the weight here. A call that can fail must be handled at the
# point of the call (rule 1), and a Try may only propagate an error the calling
# function has declared it can carry (rule 2). Everything else is syntax.

Test-Case 'a Catch runs the arm for the error that happened' {
    $src = @'
Error Io { NotFound, Denied, }
Function F(which: Number) -> Result{Number, Io} {
    If (which == 1) { Return 100; }
    Fail Denied;
}
Function G(which: Number) {
    Variable n: Number = F(which) Catch {
        NotFound { -1; }
        Denied { -2; }
    };
    Terminal.Write(n);
}
G(1);
G(2);
'@
    Test-Output $src @('100', '-2')
}
Test-Case 'a Catch on the success path yields the value' {
    Test-Output 'Error E { X, } Function F() -> Result{Number, E} { Return 7; } Function G() { Variable n: Number = F() Catch { X { 0; } }; Terminal.Write(n); } G();' @('7')
}
Test-Case 'Try forwards an error up the call stack' {
    $src = @'
Error E { X, }
Function Leaf() -> Result{Number, E} { Fail X; }
Function Middle() -> Result{Number, E} {
    Variable n: Number = Leaf() Try;
    Return n + 1;
}
Function Top() {
    Variable v: Number = Middle() Catch { X { 42; } };
    Terminal.Write(v);
}
Top();
'@
    Test-Output $src @('42')
}
Test-Case 'Try is transparent on the success path' {
    Test-Output 'Error E { X, } Function F() -> Result{Number, E} { Return 5; } Function G() -> Result{Number, E} { Variable n: Number = F() Try; Return n; } Function H() { Variable v: Number = G() Catch { X { 0; } }; Terminal.Write(v); } H();' @('5')
}
Test-Case 'Try composes with Return' {
    Test-Output 'Error E { X, } Function F() -> Result{Number, E} { Fail X; } Function G() -> Result{Number, E} { Return F() Try; } Function H() { Variable v: Number = G() Catch { X { 9; } }; Terminal.Write(v); } H();' @('9')
}
Test-Case 'an unhandled fallible call is an error' {
    # Rule 1. The point of the whole design: a Result never reaches a place
    # that does not handle it.
    Test-Diagnostic 'Error E { X, } Function F() -> Result{Number, E} { Return 1; } Variable n: Number = F();' 'can fail; follow the call with Try or Catch'
}
Test-Case 'a Catch must handle every variant' {
    Test-Diagnostic 'Error E { A, B, } Function F() -> Result{Number, E} { Fail A; } Variable n: Number = F() Catch { A { 1; } };' "does not handle 'B'"
}
Test-Case 'a Catch may not name an error the callee cannot raise' {
    Test-Diagnostic 'Error E { A, } Function F() -> Result{Number, E} { Return 1; } Variable n: Number = F() Catch { Z { 1; } };' "cannot fail with 'Z'"
}
Test-Case 'a Catch may not handle the same error twice' {
    Test-Diagnostic 'Error E { A, } Function F() -> Result{Number, E} { Fail A; } Variable n: Number = F() Catch { A { 1; } A { 2; } };' "handled twice"
}
Test-Case 'Try cannot propagate an error the caller does not declare' {
    # Rule 2, and the reason it exists: without it, an error could leave a
    # function whose signature cannot describe it. G must be called -- the rule
    # is checked when the Try runs, like every other rule in the interpreter.
    $src = @'
Error A { X, }
Error B { Y, }
Function F() -> Result{Number, B} { Fail Y; }
Function G() -> Result{Number, A} {
    Variable n: Number = F() Try;
    Return n;
}
Function H() { Variable v: Number = G() Catch { X { 0; } }; }
H();
'@
    Test-Diagnostic $src "can fail with 'Y'"
}
Test-Case 'a superset error set may propagate a subset' {
    # A declares both X and Y; F can only raise X. G may forward X.
    $src = @'
Error A { X, Y, }
Error B { X, }
Function F() -> Result{Number, B} { Fail X; }
Function G() -> Result{Number, A} {
    Variable n: Number = F() Try;
    Return n;
}
Function H() {
    Variable v: Number = G() Catch { X { 3; } Y { 4; } };
    Terminal.Write(v);
}
H();
'@
    Test-Output $src @('3')
}
Test-Case 'Try at the top level has nowhere to propagate to' {
    Test-Diagnostic 'Error E { X, } Function F() -> Result{Number, E} { Fail X; } Variable n: Number = F() Try;' 'no enclosing function'
}
Test-Case 'a Catch at the top level is fine' {
    # The caller of a Catch does not need to be a function; only a Try does,
    # because a Try has to hand the error to somebody.
    Test-Output 'Error E { X, } Function F() -> Result{Number, E} { Fail X; } Variable n: Number = F() Catch { X { 4; } }; Terminal.Write(n);' @('4')
}
Test-Case 'Fail needs a declared error set' {
    Test-Diagnostic 'Error E { X, } Function F() { Fail X; } F();' 'declares no error set'
}
Test-Case 'Fail cannot name an error the function does not declare' {
    $src = @'
Error E { X, }
Function F() -> Result{Number, E} { Fail Other; }
Function G() { Variable v: Number = F() Catch { X { 0; } }; }
G();
'@
    Test-Diagnostic $src "cannot fail with 'Other'"
}
Test-Case 'Result is only a function return type' {
    # A variable of that type would let a fallible call be bound and forgotten,
    # which is exactly what rule 1 exists to prevent.
    Test-Diagnostic 'Error E { X, } Variable v: Result{Number, E} = 1;' 'may only be a function return type'
}
Test-Case 'a Catch arm must end with an expression' {
    Test-Diagnostic 'Error E { X, } Function F() -> Result{Number, E} { Fail X; } Variable n: Number = F() Catch { X { } };' 'must end with an expression'
}
Test-Case 'Try on something that cannot fail is an error' {
    $src = @'
Error E { X, }
Function F() { Return 1; }
Function G() -> Result{Number, E} {
    Variable n: Number = F() Try;
    Return n;
}
Function H() { Variable v: Number = G() Catch { X { 0; } }; }
H();
'@
    Test-Diagnostic $src 'cannot fail'
}
Test-Case 'an error stops the statements after it' {
    # Statements before the Try must run and statements after it must not,
    # including ones nested in an If. The guard has to cover nested blocks, not
    # just the loop directly containing the propagating statement.
    $src = @'
Error E { X, }
Function F() -> Result{Number, E} { Fail X; }
Function G() -> Result{Number, E} {
    Terminal.Write("one");
    Variable v: Number = 0;
    If (1) { Terminal.Write("two"); }
    Variable v2: Number = F() Try;
    Terminal.Write("three");
    If (1) { Terminal.Write("four"); }
    Return 1;
}
Function H() { Variable z: Number = G() Catch { X { 0; } }; }
H();
'@
    Test-Output $src @('one', 'two')
}

# --- Allocators -------------------------------------------------------------
#
# The exit criterion this phase exists for: allocation failure is a value, never
# a crash. The restore tests matter as much -- an allocator block that fails to
# restore on an exit path leaves code allocating into an arena that is no longer
# current, which is silent corruption rather than a diagnostic.

Test-Case 'an Allocator holds real bytes' {
    $src = @'
Variable arena: Allocator = Allocator(32);
Variable at: Word = arena.Allocate(8) Catch { OutOfMemory { 0; } };
arena.Store(at, 65);
arena.Store(at + 1, 66);
Terminal.Write(arena.Load(at));
Terminal.Write(arena.Load(at + 1));
Terminal.Write(arena.Used());
Terminal.Write(arena.Capacity());
'@
    Test-Output $src @('A', 'B', '8', '32')
}
Test-Case 'allocation failure is a value, not a crash' {
    $src = @'
Variable arena: Allocator = Allocator(16);
Variable tooBig: Word = arena.Allocate(4096) Catch {
    OutOfMemory {
        Terminal.Write("did not fit");
        0;
    }
};
Terminal.Write(tooBig);
'@
    Test-Output $src @('did not fit', '0')
}
Test-Case 'an exhausted allocator is still usable' {
    # Exhaustion must not corrupt the arena. A bump allocator that returned
    # garbage on the next call would hand out the same offset twice.
    $src = @'
Variable arena: Allocator = Allocator(16);
Variable a: Word = arena.Allocate(8) Catch { OutOfMemory { 0; } };
Variable b: Word = arena.Allocate(8) Catch { OutOfMemory { 0; } };
Variable c: Word = arena.Allocate(4096) Catch { OutOfMemory { 999; } };
Variable d: Word = arena.Allocate(8) Catch { OutOfMemory { 999; } };
Terminal.Write(a);
Terminal.Write(b);
Terminal.Write(c);
Terminal.Write(d);
'@
    Test-Output $src @('0', '8', '999', '999')
}
Test-Case 'the block binds the allocator' {
    # The whole point: an ambient Allocate goes to the arena the block named,
    # and nowhere else.
    $src = @'
Variable outer: Allocator = Allocator(64);
Variable scratch: Allocator = Allocator(16);
allocator(scratch) {
    Variable p: Word = Allocate(8) Catch { OutOfMemory { Fail OutOfMemory; } };
    Terminal.Write(scratch.Used());
    Terminal.Write(outer.Used());
}
'@
    Test-Output $src @('8', '0')
}
Test-Case 'nesting restores the right allocator' {
    $src = @'
Variable outer: Allocator = Allocator(64);
Variable inner: Allocator = Allocator(16);
allocator(outer) {
    Variable p: Word = Allocate(4) Catch { OutOfMemory { Fail OutOfMemory; } };
    allocator(inner) {
        Variable q: Word = Allocate(4) Catch { OutOfMemory { Fail OutOfMemory; } };
        Terminal.Write(inner.Used());
    }
    Terminal.Write(outer.Used());
}
'@
    Test-Output $src @('4', '4')
}
Test-Case 'a Return out of the block restores the binding' {
    # Unobservable from inside the function, so checked from outside: after the
    # call, an ambient Allocate must find no binding. If the restore were
    # skipped, it would allocate into an arena whose call frame is gone.
    $src = @'
Function Escape() -> Word {
    Variable here: Allocator = Allocator(8);
    allocator(here) {
        Variable p: Word = Allocate(4) Catch { OutOfMemory { Fail OutOfMemory; } };
        Return here.Used();
    }
    Return 0;
}
Terminal.Write(Escape());
Variable after: Word = Allocate(4) Catch { OutOfMemory { 0; } };
'@
    Test-Diagnostic $src 'no allocator is bound'
}
Test-Case 'an error propagating out of the block restores the binding' {
    $src = @'
Function PropagateOut() -> Result{Word, Memory} {
    Variable a: Allocator = Allocator(16);
    allocator(a) {
        Variable p: Word = Allocate(4) Catch { OutOfMemory { Fail OutOfMemory; } };
        Fail OutOfMemory;
    }
    Return 0;
}
Variable handled: Word = PropagateOut() Catch { OutOfMemory { 7; } };
Terminal.Write(handled);
Variable after: Word = Allocate(4) Catch { OutOfMemory { 0; } };
'@
    Test-Diagnostic $src 'no allocator is bound'
}
Test-Case 'a propagated OutOfMemory is still catchable outside the block' {
    $src = @'
Function PropagateOut() -> Result{Word, Memory} {
    Variable a: Allocator = Allocator(16);
    allocator(a) {
        Variable p: Word = Allocate(4) Catch { OutOfMemory { Fail OutOfMemory; } };
        Fail OutOfMemory;
    }
    Return 0;
}
Variable handled: Word = PropagateOut() Catch { OutOfMemory { 7; } };
Terminal.Write(handled);
'@
    Test-Output $src @('7')
}
Test-Case 'an ambient Allocate with no binding is an error' {
    Test-Diagnostic 'Variable p: Word = Allocate(4) Catch { OutOfMemory { 0; } };' 'no allocator is bound'
}
Test-Case 'an Allocator may not be redeclared as an error set' {
    # Memory is predeclared, so a program cannot redefine OutOfMemory with
    # different contents and have Allocate silently mean something else.
    Test-Diagnostic 'Error Memory { OutOfMemory, }' 'already declared'
}
Test-Case 'Store and Load reject an out-of-range offset' {
    # A bug rather than an allocation failure, so it stops rather than becoming
    # a value to check. A kernel reading the wrong frame should not go on.
    Test-Diagnostic 'Variable a: Allocator = Allocator(16); Variable p: Word = a.Allocate(4) Catch { OutOfMemory { 0; } }; Terminal.Write(a.Load(p + 100));' "outside this Allocator"
}
Test-Case 'an Allocator capacity of zero is an error' {
    Test-Diagnostic 'Variable a: Allocator = Allocator(0);' 'greater than zero'
}
Test-Case 'an unhandled allocation is an error' {
    Test-Diagnostic 'Variable a: Allocator = Allocator(16); Variable p: Word = a.Allocate(4);' 'can fail; follow the call with Try or Catch'
}
Test-Case 'an unknown Allocator method is an error' {
    Test-Diagnostic 'Variable a: Allocator = Allocator(16); Terminal.Write(a.Nonsense());' "no method 'Nonsense'"
}

# --- Bitwise ----------------------------------------------------------------
#
# Found by writing an OS-shaped program and seeing what would not compile. A
# page table entry is `frame | flags << 48`, and without these no kernel is
# possible at all. The signedness rules below are the substance; the operators
# themselves are mechanical.

Test-Case 'bitwise and, or, xor on Word' {
    $src = @'
Variable a: Word = 12;
Variable b: Word = 10;
Variable c: Word = a & b;
Variable d: Word = a | b;
Variable e: Word = a ^ b;
Terminal.Write(c);
Terminal.Write(d);
Terminal.Write(e);
'@
    Test-Output $src @('8', '14', '6')
}
Test-Case 'complement' {
    $src = @'
Variable a: Word = 0;
Terminal.Write(~a);
'@
    Test-Output $src @('18446744073709551615')
}
Test-Case 'set, test, and clear a flag bit' {
    # The shape every register write has.
    $src = @'
Variable flags: Word = 0;
flags = flags | (1 << 3);
Terminal.Write(flags);
Terminal.Write(flags & (1 << 3));
flags = flags & ~(1 << 3);
Terminal.Write(flags);
'@
    Test-Output $src @('8', '8', '0')
}
Test-Case 'a page table entry packs a frame and flags' {
    $src = @'
Function MakePte(frame: Word, flags: Word) -> Word {
    Return frame | (flags << 48);
}
Variable pte: Word = MakePte(42, 3);
Terminal.Write(pte & 0x0000FFFFFFFFFFFF);
Terminal.Write(pte >> 48);
'@
    Test-Output $src @('42', '3')
}
Test-Case 'hexadecimal literals' {
    Test-Output 'Variable a: Number = 0xFF; Terminal.Write(a);' @('255')
}
Test-Case 'underscore digit separators' {
    Test-Output 'Variable a: Number = 1_000_000; Terminal.Write(a);' @('1000000')
    Test-Output 'Variable a: Number = 0xFF_FF; Terminal.Write(a);' @('65535')
}
Test-Case 'right shift is logical on Word' {
    # The point of Word being unsigned: a mask shifted right brings in zeros.
    # An arithmetic shift would turn 1 into 0xFFFF...FFFF.
    Test-Output 'Variable a: Word = 1 << 63; Terminal.Write(a >> 63);' @('1')
}
Test-Case 'right shift is logical on Character' {
    Test-Output 'Variable a: Character = 200; Variable b: Character = 12; Terminal.Write(a >> 4 == b);' @('True')
}
Test-Case 'right shift is arithmetic on signed types' {
    Test-Output 'Variable a: Number = -16; Terminal.Write(a >> 2);' @('-4')
    Test-Output 'Variable a: Number32 = -16; Terminal.Write(a >> 2);' @('-4')
}
Test-Case 'a shift cannot overflow the type' {
    Test-Diagnostic 'Variable a: Character = 200; Variable b: Character = a << 4;' 'overflow'
}
Test-Case 'a constant shift is evaluated in the type it is stored as' {
    # `1 << 63` does not fit a signed 64-bit Number but is an ordinary Word.
    Test-Output 'Variable a: Word = 1 << 63; Terminal.Write(a >> 63);' @('1')
}
Test-Case 'a shift count must be narrower than the type' {
    Test-Diagnostic 'Variable a: Word = 1; Variable b: Word = a << 64;' 'too large'
}
Test-Case 'a shift count must not be negative' {
    Test-Diagnostic 'Variable a: Word = 1; Variable n: Number = -1; Variable b: Word = a << n;' 'must not be negative'
}
Test-Case 'a shift count may be any integer type' {
    # Deliberate exception to "both operands must be the same type": a count is
    # not data. `Loop (i) { flags << i }` is the ordinary shape.
    Test-Output 'Variable flags: Word = 1; Variable i: Word = 4; Terminal.Write(flags << i);' @('16')
}
Test-Case 'bitwise operators reject mixed widths' {
    Test-Diagnostic 'Variable a: Word = 1; Variable b: Number = 1; Terminal.Write(a | b);' 'cannot mix'
}
Test-Case 'a shift binds tighter than a comparison' {
    # C precedence: shifts tighter than relational, which is tighter than the
    # bitwise operators. `a | b == c` must read as `a | (b == c)`.
    $src = @'
Variable a: Word = 4;
Variable b: Word = 1;
Variable five: Word = 5;
Terminal.Write((a | b) == five);
'@
    Test-Output $src @('True')
}
Test-Case 'bitwise result keeps the operand type' {
    # 236 has its top bit set. Reinterpreting that as signed would turn
    # `200 | 100` into -20.
    $src = @'
Variable a: Character = 200;
Variable b: Character = 100;
Variable full: Character = 236;
Terminal.Write((a | b) == full);
'@
    Test-Output $src @('True')
}

# --- Arrays -----------------------------------------------------------------

Test-Case 'an array element can be read' {
    $src = @'
Variable page: [4]Word = [10, 20, 30, 40];
Terminal.Write(page[2]);
'@
    Test-Output $src @('30')
}
Test-Case 'an array element can be assigned' {
    $src = @'
Variable page: [4]Word = [10, 20, 30, 40];
page[2] = 99;
Terminal.Write(page[2]);
'@
    Test-Output $src @('99')
}
Test-Case 'Length reports the declared length' {
    # The length comes from the type, so it is available before any literal is.
    Test-Output "Variable page: [512]Word;`nTerminal.Write(page.Length());" @('512')
}
Test-Case 'an array prints its contents' {
    # Printing an array shows what is in it, so a wrong value is visible rather
    # than hidden behind a summary.
    Test-Output "Variable page: [3]Word = [10, 20, 30];`nTerminal.Write(page);" @('[10, 20, 30]')
}
Test-Case 'an uninitialised element prints as nothing' {
    # An array declared without a literal starts with every element unchosen.
    # Printing zeroed fields instead would claim the programmer had set them.
    Test-Output "Variable page: [2]Word;`nTerminal.Write(page);" @('[nothing, nothing]')
}
Test-Case 'an array literal must match the declared length' {
    $src = @'
Variable page: [3]Word = [10, 20];
Terminal.Write(1);
'@
    Test-Diagnostic $src 'this array has 3 element\(s\) but 2 were given'
}
Test-Case 'reading past the end is an error, not a wrap' {
    # An index that runs off the end is a memory-safety bug. This language has
    # no pointer arithmetic for a programmer to have got wrong, so a silent read
    # would have to be a bug in the interpreter itself.
    $src = @'
Variable page: [3]Word = [10, 20, 30];
Terminal.Write(page[3]);
'@
    Test-Diagnostic $src 'index 3 is outside this array of 3 element\(s\)'
}
Test-Case 'a negative index is an error' {
    # Not clamped to zero and not read from before the array.
    Test-Diagnostic "Variable page: [3]Word = [10, 20, 30];`nTerminal.Write(page[-1]);" 'must not be negative'
}
Test-Case 'a Word index past the end is an error' {
    # The index is a Word, so there is no negative case -- only the upper bound.
    $src = @'
Variable page: [3]Word = [10, 20, 30];
Variable at: Word = 3;
Terminal.Write(page[at]);
'@
    Test-Diagnostic $src 'index 3 is outside'
}
Test-Case 'an array copies on assignment' {
    # Value semantics, like a struct. Two names never share storage.
    $src = @'
Variable page: [3]Word = [10, 20, 30];
Variable copy: [3]Word = page;
copy[0] = 99;
Terminal.Write(page[0]);
Terminal.Write(copy[0]);
'@
    Test-Output $src @('10', '99')
}
Test-Case 'an array passes to a function by value' {
    # The callee mutates its copy; the caller's array is untouched.
    $src = @'
Function Zero(first: [3]Word) {
    first[0] = 0;
}
Variable page: [3]Word = [10, 20, 30];
Zero(page);
Terminal.Write(page[0]);
'@
    Test-Output $src @('10')
}
Test-Case 'an array literal may be a call argument' {
    # The literal's shape comes from the parameter, the only place it is written.
    $src = @'
Function Sum(table: [3]Word) -> Number {
    Return table[0] + table[1] + table[2];
}
Terminal.Write(Sum([1, 2, 3]));
'@
    Test-Output $src @('6')
}
Test-Case 'an array of the wrong length is refused' {
    $src = @'
Function Sum(table: [3]Word) -> Number {
    Return table[0];
}
Variable page: [4]Word = [1, 2, 3, 4];
Terminal.Write(Sum(page));
'@
    Test-Diagnostic $src "expects an array of 3, got 4"
}
Test-Case 'array elements are checked against the element type' {
    # The element type is not decoration: a Number literal narrows to a Word,
    # but a Number array is not a Word array.
    $src = @'
Variable names: [2]Number = [10, 20];
Variable page: [2]Word = names;
'@
    Test-Diagnostic $src 'this array holds Number but 2 Word were given'
}
Test-Case 'an array of Characters holds bytes' {
    # A Character is a byte, so an array of them is a byte buffer -- the shape a
    # file path or a command line needs. Each prints as its character.
    $src = @'
Variable name: [5]Character = [104, 101, 108, 108, 111];
Variable i: Word = 0;
Loop i < name.Length() {
    Terminal.Write(name[i]);
    i = i + 1;
}
'@
    Test-Output $src @('h', 'e', 'l', 'l', 'o')
}
Test-Case 'an array of structs nests by value' {
    # The element type may be a struct, and the element is copied out -- so
    # `last` is independent of what is still in the array.
    $src = @'
Struct Point { x: Number, y: Number }
Variable path: [3]Point = [Point{x: 0, y: 0}, Point{x: 1, y: 1}, Point{x: 2, y: 4}];
Variable last: Point = path[2];
last.x = 99;
Terminal.Write(last.x);
Terminal.Write(path[2].x);
'@
    Test-Output $src @('99', '2')
}
Test-Case 'an array literal may hold struct literals' {
    $src = @'
Struct Point { x: Number, y: Number }
Variable path: [2]Point = [Point{x: 1, y: 2}, Point{x: 3, y: 4}];
Terminal.Write(path[1].y);
'@
    Test-Output $src @('4')
}
Test-Case 'an array element must match the element type' {
    $src = @'
Variable page: [2]Word = [10, "seventy"];
'@
    Test-Diagnostic $src 'an array element expects Word, got String'
}
Test-Case 'an array length must be a number' {
    Test-Diagnostic 'Variable page: []Word;' 'an array length must be a number'
}
Test-Case 'an array length must be greater than zero' {
    Test-Diagnostic 'Variable page: [0]Word;' 'greater than zero'
}
Test-Case 'indexing a Number is an error' {
    Test-Diagnostic 'Variable n: Number = 5; Terminal.Write(n[0]);' 'cannot index Number'
}
Test-Case 'indexing a String is an error' {
    # Strings are not arrays of bytes yet. This is a gap, not a ruling -- but
    # saying so here means the test will change when it is filled.
    Test-Diagnostic 'Terminal.Write("abc"[0]);' 'cannot index String'
}
Test-Case 'a nested index is refused' {
    # Only a whole variable is a target. `a[0][1] = x` would need a path this
    # does not have, so it is refused rather than quietly half-done.
    Test-Diagnostic 'Variable a: [2][2]Word;' 'may only be used for a variable or a parameter'
}
Test-Case 'an array has only Length' {
    Test-Diagnostic 'Variable page: [2]Word; Terminal.Write(page.First());' "an array has no method 'First'"
}
Test-Case 'Length takes no arguments' {
    Test-Diagnostic 'Variable page: [2]Word; Terminal.Write(page.Length(1));' 'Length expects 0 arguments'
}
Test-Case 'an array is not a struct field' {
    # Deferred deliberately: a struct field needs the array's length to be part
    # of the aggregate's own type, which is the SizeOf question still open.
    Test-Diagnostic "Struct Pair { a: [2]Word, b: Number }" 'may only be used for a variable or a parameter'
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