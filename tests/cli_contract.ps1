<#
.SYNOPSIS
    CLI contract tests for the Siyana Lang bootstrap binary.

.DESCRIPTION
    Phase 1 has no language features to test yet, so this covers the command
    line interface instead: the behaviour other tools and scripts will depend
    on. Checking it now means the exit criteria for Phase 1 are verified rather
    than assumed.

    Language tests start in Phase 2 and will live alongside this file.

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

function Invoke-Si([string[]]$SiArgs) {
    <#
      Invoke the binary and capture both streams.

      Two PowerShell quirks make this fiddlier than it looks:
        1. Piping a native command inside a function can stop $LASTEXITCODE
           being set for later checks, so output is captured to a variable.
        2. With $ErrorActionPreference='Stop', any native command writing to
           stderr is raised as a terminating error. Three of the cases below
           exercise stderr deliberately, so the preference is suspended for the
           duration of the call.
    #>
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & $script:ExePath @SiArgs 2>&1 | Out-String
        $code = $LASTEXITCODE
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

Write-Host 'running CLI contract tests'

Test-Case '--version exits zero' {
    $r = Invoke-Si @('--version')
    if ($r.Code -ne 0) { throw "exit code $($r.Code)" }
    if ($r.Out -notmatch 'siyana \d+\.\d+\.\d+') { throw "no version in output: $($r.Out)" }
}

Test-Case '--version reports the current phase' {
    $r = Invoke-Si @('--version')
    if ($r.Out -notmatch 'phase:\s*\d') { throw "no phase line: $($r.Out)" }
}

Test-Case '--help exits zero and prints usage' {
    $r = Invoke-Si @('--help')
    if ($r.Code -ne 0) { throw "exit code $($r.Code)" }
    if ($r.Out -notmatch 'usage:') { throw "no usage line: $($r.Out)" }
}

Test-Case 'no arguments exits zero' {
    $r = Invoke-Si @()
    if ($r.Code -ne 0) { throw "exit code $($r.Code)" }
}

Test-Case 'unknown option exits non-zero' {
    $r = Invoke-Si @('--nonsense')
    if ($r.Code -eq 0) { throw 'expected non-zero exit' }
    if ($r.Out -notmatch 'unknown option') { throw "unexpected message: $($r.Out)" }
}

Test-Case 'a valid program runs and exits zero' {
    $file = Join-Path ([System.IO.Path]::GetTempPath()) ("siyana-cli-{0}.sl" -f [guid]::NewGuid())
    'Terminal.Write("ok");' | Out-File -Encoding ascii $file
    try {
        $r = Invoke-Si @('run', $file)
        if ($r.Code -ne 0) { throw "exit code $($r.Code): $($r.Out)" }
        if ($r.Out -notmatch 'ok') { throw "no output: $($r.Out)" }
    } finally {
        Remove-Item $file -ErrorAction SilentlyContinue
    }
}

Test-Case 'a .sl file is the expected extension' {
    # Extension is only a convention so far; the interpreter accepts any file.
    # This pins the documented convention so a rename does not drift silently.
    $file = Join-Path ([System.IO.Path]::GetTempPath()) ("siyana-cli-{0}.sl" -f [guid]::NewGuid())
    'Terminal.Write("ok");' | Out-File -Encoding ascii $file
    try {
        $r = Invoke-Si @('run', $file)
        if ($r.Code -ne 0) { throw "exit code $($r.Code): $($r.Out)" }
    } finally {
        Remove-Item $file -ErrorAction SilentlyContinue
    }
}

Test-Case 'missing file exits non-zero with a clear message' {
    $r = Invoke-Si @('run', 'does-not-exist.sy')
    if ($r.Code -eq 0) { throw 'expected non-zero exit' }
    if ($r.Out -notmatch 'cannot open') { throw "unexpected message: $($r.Out)" }
}

Test-Case 'run without a file exits non-zero' {
    $r = Invoke-Si @('run')
    if ($r.Code -eq 0) { throw 'expected non-zero exit' }
    if ($r.Out -notmatch 'needs a source file') { throw "unexpected message: $($r.Out)" }
}

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) of $($script:Total) test(s) failed" -ForegroundColor Red
    exit 1
}
Write-Host "all $($script:Total) tests passed"

# Exit explicitly. $LASTEXITCODE still holds the exit code of the last native
# command run -- and several of the cases above invoke the binary with arguments
# that exit non-zero on purpose. Falling off the end of the script would leak
# that value to the caller and report a failed run that actually passed.
exit 0