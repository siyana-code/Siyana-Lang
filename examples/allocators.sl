// examples/allocators.sl -- explicit allocators with lexically scoped binding.
//
// Run with: siyana run examples/allocators.sl
//
// `Memory { OutOfMemory }` is predeclared, so it is not written here.

Terminal.Write("--- an allocator hands out real bytes ---");

Variable arena: Allocator = Allocator(32);
Terminal.Write(arena.Capacity());

Variable first: Word = arena.Allocate(8) Catch {
    OutOfMemory { Terminal.Write("impossible"); 0; }
};
Terminal.Write(first);

arena.Store(first, 65);
arena.Store(first + 1, 66);

Variable byteA: Character = arena.Load(first);
Variable byteB: Character = arena.Load(first + 1);
Terminal.Write(byteA);
Terminal.Write(byteB);
Terminal.Write(arena.Used());

Terminal.Write("--- exhaustion is a value, not a crash ---");

Variable tooBig: Word = arena.Allocate(4096) Catch {
    OutOfMemory { Terminal.Write("  4096 does not fit in 32"); 0; }
};
Terminal.Write(tooBig);
Terminal.Write("  the allocator is still usable: ");
Variable after: Word = arena.Allocate(4) Catch {
    OutOfMemory { 0; }
};
Terminal.Write(after);

Terminal.Write("--- the block binds the allocator ---");

Variable outer: Allocator = Allocator(64);
Variable scratch: Allocator = Allocator(16);

allocator(scratch) {
    Variable implicit: Word = Allocate(8) Catch {
        OutOfMemory { Fail OutOfMemory; }
    };
    Terminal.Write("  inside the block, scratch.Used() = ", scratch.Used());
    Terminal.Write("  inside the block, outer.Used() = ", outer.Used());
}

Terminal.Write("outside the block, scratch.Used() = ", scratch.Used());

Terminal.Write("--- nesting restores the right allocator ---");

allocator(outer) {
    Terminal.Write("  outer bound, outer.Used() = ", outer.Used());
    allocator(scratch) {
        Variable p: Word = Allocate(4) Catch { OutOfMemory { Fail OutOfMemory; } };
        Terminal.Write("  inner bound, scratch.Used() = ", scratch.Used());
    }
    Terminal.Write("  inner left, outer.Used() = ", outer.Used());
}

Terminal.Write("--- a Return out of the block still restores ---");

Function EscapeFromBlock() -> Word {
    Variable here: Allocator = Allocator(8);
    allocator(here) {
        Variable p: Word = Allocate(4) Catch { OutOfMemory { Fail OutOfMemory; } };
        Return here.Used();
    }
    Return 0;
}
Terminal.Write("used before the early return: ", EscapeFromBlock());

Terminal.Write("--- an error propagating out restores the binding ---");

Function PropagateOut() -> Result{Word, Memory} {
    Variable a: Allocator = Allocator(16);
    allocator(a) {
        Variable p: Word = Allocate(4) Catch { OutOfMemory { Fail OutOfMemory; } };
        Fail OutOfMemory;
    }
    Return 0;
}

Variable afterError: Word = PropagateOut() Catch {
    OutOfMemory { 0; }
};
Terminal.Write("  handled the propagated OutOfMemory: ", afterError);

// The last section deliberately fails. Both the Return path above and the
// propagating path here left the binding restored to nothing, so an ambient
// Allocate has no allocator to use. If either restore were skipped, this would
// silently allocate into an arena that is no longer current -- which is exactly
// the bug this block form exists to prevent.
Terminal.Write("--- a binding is required ---");

Variable afterReturn: Word = Allocate(4) Catch {
    OutOfMemory { 0; }
};