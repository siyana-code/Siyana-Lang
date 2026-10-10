// examples/errors.sl -- error sets, Try, Catch, and Fail.
//
// Run with: siyana run examples/errors.sl
//
// The last two sections deliberately fail. They are there to show what the
// diagnostics look like, so the program ends non-zero.

Error Io {
    NotFound,
    Denied,
    Corrupt,
}

// A leaf function. `Fail` is how a function originates an error at all --
// Try only propagates, so without it nothing could ever fail.
Function ReadConfig(which: Number) -> Result{Number, Io} {
    If (which == 1) {
        Return 100;
    }
    If (which == 2) {
        Fail Denied;
    }
    Fail NotFound;
}

// A middle function. It does not care which error happened; it forwards
// whatever ReadConfig raises. That is what Try is for.
Function Load(which: Number) -> Result{Number, Io} {
    Variable raw: Number = ReadConfig(which) Try;
    Return raw * 2;
}

Terminal.Write("--- Catch handles every variant ---");

Variable denied: Number = Load(2) Catch {
    NotFound { -1; }
    Denied {
        Terminal.Write("  Load(2) failed with Denied");
        -2;
    }
    Corrupt { -3; }
};
Terminal.Write("denied = ", denied);

Terminal.Write("--- Catch leaves the success path alone ---");

Variable fine: Number = Load(1) Catch {
    NotFound { -1; }
    Denied { -2; }
    Corrupt { -3; }
};
Terminal.Write("fine = ", fine);

Terminal.Write("--- Try is transparent when nothing goes wrong ---");

Function Twice(which: Number) -> Result{Number, Io} {
    Return Load(which) Try;
}

Variable doubled: Number = Twice(1) Catch {
    NotFound { 0; }
    Denied { 0; }
    Corrupt { 0; }
};
Terminal.Write("doubled = ", doubled);

Terminal.Write("--- a call that can fail must be handled ---");
// 'Load(2)' is never wrapped, so the error has nowhere to go.

Variable ignored: Number = Load(2);

Terminal.Write("--- a Catch that misses a variant ---");
// Corrupt is no longer handled, so adding it to Io would break this program.

Variable partial: Number = Load(2) Catch {
    NotFound { -1; }
    Denied { -2; }
};

Terminal.Write("--- Try with no enclosing function ---");
// The top level declares no error set, so there is nowhere to propagate to.

Variable leaked: Number = Load(2) Try;