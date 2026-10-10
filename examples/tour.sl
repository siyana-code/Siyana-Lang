// examples/tour.sl -- a tour of everything implemented so far.
//
// Run with: siyana run examples/tour.sl

Function Factorial(n: Number) -> Number {
    If (n <= 1) {
        Return 1;
    }
    Return n * Factorial(n - 1);
}

Function Describe(n: Number) {
    If (n < 0) {
        Terminal.Write("negative");
    } Else If (n == 0) {
        Terminal.Write("zero");
    } Else {
        Terminal.Write("positive");
    }
}

Function Greet(times: Number) {
    Variable i: Number = 0;
    Loop (i < times) {
        Terminal.Write("hello");
        i = i + 1;
    }
}

Terminal.Write("--- literals ---");
Terminal.Write(42);
Terminal.Write(-7);
Terminal.Write(True);
Terminal.Write(False);

Terminal.Write("--- Character ---");
Variable letter: Character = 'A';
Terminal.Write(letter);

Terminal.Write("--- arithmetic ---");
Terminal.Write(2 + 3 * 4);
Terminal.Write((2 + 3) * 4);
Terminal.Write(17 / 5);

Terminal.Write("--- comparison ---");
Terminal.Write(3 == 3);
Terminal.Write(3 != 3);
Terminal.Write(2 < 5);
Terminal.Write(5 >= 5);
Terminal.Write("ab" == "ab");

Terminal.Write("--- C-style truthiness ---");
Variable one: Number = 1;
If (one) {
    Terminal.Write("1 is True");
}
Variable zero: Number = 0;
If (zero) {
    Terminal.Write("0 is True");
} Else {
    Terminal.Write("0 is False");
}

Terminal.Write("--- variables ---");
Variable count: Number = 3;
count = count + 1;
Terminal.Write(count);

Terminal.Write("--- functions ---");
Greet(2);
Terminal.Write(Factorial(5));
Describe(0);
Describe(-4);
Describe(9);