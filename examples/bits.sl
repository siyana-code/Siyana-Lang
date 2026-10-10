// examples/bits.sl -- bitwise arithmetic on fixed-width integers.
//
// Run with: siyana run examples/bits.sl
//
// Everything here is bit-pattern work: register flags, masks, and field
// extraction. That is most of what a kernel does to hardware, so it is the
// reason the operators carry the same overflow rules as arithmetic.

Terminal.Write("--- bitwise on Word ---");

Variable flags: Word = 0;
flags = flags | 1;                    // bit 0
flags = flags | (1 << 3);             // bit 3
Terminal.Write(flags);                // 9

Variable present: Word = flags & (1 << 3);
Terminal.Write(present);              // 8

flags = flags & ~(1 << 3);            // clear bit 3
Terminal.Write(flags);                // 1

flags = flags ^ 1;                    // flip bit 0
Terminal.Write(flags);                // 0

Terminal.Write("--- a page table entry ---");

// The shape real kernel code has: a frame number and flags packed into one
// word. Every field is a Word, because that is what the hardware calls for,
// and decision 20's rule -- mixed widths are an error -- is what stops the
// frame being silently truncated.

Function MakePte(frame: Word, flags: Word) -> Word {
    Return frame | (flags << 48);
}

Function FrameOf(pte: Word) -> Word {
    Return pte & 0x0000FFFFFFFFFFFF;
}

Variable pte: Word = MakePte(42, 3);
Terminal.Write(FrameOf(pte));         // 42
Terminal.Write(pte >> 48);            // 3

Terminal.Write("--- right shift is logical on unsigned types ---");

// The whole point of Word being unsigned. A mask shifted right must bring in
// zeros, not ones: arithmetic shift would turn 1 into 0xFFFF...FFFF.
Variable high: Word = 1 << 63;
Terminal.Write(high >> 63);            // 1, not 0x8000... shifted down

Variable byte: Character = 200;
Variable quarter: Character = 12;
Terminal.Write(byte >> 4 == quarter);   // 200 >> 4 is 12

Terminal.Write("--- right shift is arithmetic on signed types ---");

// A programmer dividing a negative number by two means the sign to replicate,
// and that is what every real compiler does.
Variable negative: Number = -16;
Terminal.Write(negative >> 2);        // -4

Variable small: Number32 = -16;
Terminal.Write(small >> 2);           // -4

Terminal.Write("--- the type is preserved ---");

Variable a: Character = 200;
Variable b: Character = 100;
Variable full: Character = 236;
Terminal.Write((a | b) == full);           // still a Character

Terminal.Write("--- overflow is an error, not a truncation ---");

// `Character << 4` would push bits out of a byte. C would silently drop them,
// and a dropped flag bit is a security bug that reads as working code.

Variable shifted: Character = 200;
Variable lost: Character = shifted << 4;