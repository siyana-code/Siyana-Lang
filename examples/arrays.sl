// examples/arrays.sl -- fixed-size arrays of values.
//
// Run with: siyana run examples/arrays.sl
//
// An array's length is part of its type, which is what makes it usable as a
// page table or a register block: the size is known before any value exists,
// and it cannot change underneath a reader. See decision 26.

Terminal.Write("--- declaring and reading ---");

Variable page: [4]Word = [10, 20, 30, 40];
Terminal.Write(page[0]);
Terminal.Write(page[3]);

// The length comes from the type, so it is available even when no literal was
// given -- which is the case a kernel hits when it sizes a buffer from a
// constant it has not been handed yet.
Variable empty: [512]Word;
Terminal.Write(empty.Length());

Terminal.Write("--- writing an element ---");

page[1] = 99;
Terminal.Write(page[1]);

Terminal.Write("--- an array is a value, not a reference ---");

// The copy is independent. Writing through it does not reach back.
Variable copy: [4]Word = page;
copy[0] = 1;
Terminal.Write(page[0]);        // still 10
Terminal.Write(copy[0]);        // 1

Terminal.Write("--- an array of Characters is a byte buffer ---");

Variable name: [5]Character = [104, 101, 108, 108, 111];
Variable i: Word = 0;
Loop i < name.Length() {
    Terminal.Write(name[i]);
    i = i + 1;
}

Terminal.Write("--- passing to a function ---");

// By value, like every other value type. The callee mutates its own copy.
Function Sum(table: [4]Word) -> Word {
    Return table[0] + table[1] + table[2] + table[3];
}

Terminal.Write(Sum([1, 2, 3, 4]));

Terminal.Write("--- an array of structs ---");

Struct Entry {
    frame: Word,
    flags: Word,
}

Variable table: [3]Entry = [Entry{frame: 0, flags: 0}, Entry{frame: 1, flags: 3}, Entry{frame: 2, flags: 3}];
Terminal.Write(table[2].frame);
Terminal.Write(table[2].flags);

Terminal.Write("--- bounds are checked, every time ---");

// This language has no pointer arithmetic, so there is no pointer-arithmetic
// mistake for a programmer to make. The check here defends against a bug in the
// interpreter itself, and for that reason it cannot be switched off.
//
// Variable page: [4]Word;
// Terminal.Write(page[4]);