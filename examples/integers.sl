// examples/integers.sl -- the integer types and what they guarantee.
//
// Run with: siyana run examples/integers.sl

Terminal.Write("--- types ---");

Variable byte: Character = 65;
Variable small: Number32 = 2147483647;
Variable big: Number = 9007199254740993;
Variable size: Word = 4096;

Terminal.Write(byte);
Terminal.Write(small);
Terminal.Write(big);
Terminal.Write(size);

Terminal.Write("--- Word is unsigned ---");
Variable remaining: Word = 5;
Loop (remaining > 0) {
    remaining = remaining - 1;
}
Terminal.Write(remaining);

Terminal.Write("--- overflow is an error, not a wrap ---");
// The next statement would wrap to -2147483648. Uncomment to see the error.
Variable danger: Number32 = 2147483647;
// danger = danger + 1;   // Error: overflow in Number32 arithmetic

Terminal.Write(danger);

Terminal.Write("--- mixed widths do not mix ---");
Variable a: Number32 = 10;
Variable b: Number = 10;
// Terminal.Write(a + b);   // Error: both operands must be the same type

Terminal.Write(a + a);
Terminal.Write(b + b);

Terminal.Write("--- literals take the declared type ---");
Variable small_byte: Character = 5;   // 5 fits in a byte
Terminal.Write(small_byte);
// Variable bad: Character = 300;      // Error: 300 does not fit a Character