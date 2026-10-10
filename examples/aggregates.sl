// examples/aggregates.sl -- structs, unions, and Match.
//
// Run with: siyana run examples/aggregates.sl

Struct Point {
    x: Number32,
    y: Number32,
}

Union Value {
    Nothing,
    Number(Number),
    Flag(Number),
}

Function Describe(v: Value) {
    Match v {
        Value.Nothing {
            Terminal.Write("nothing");
        }
        Value.Number(n) {
            Terminal.Write("number ", n);
        }
        Value.Flag(f) {
            Terminal.Write("flag ", f);
        }
    }
}

Terminal.Write("--- structs ---");
Variable p: Point = Point { x: 3, y: 4 };
Terminal.Write(p);
Terminal.Write(p.x);
Terminal.Write(p.y);

Terminal.Write("--- field order does not matter ---");
Variable q: Point = Point { y: 1, x: 2 };
Terminal.Write(q);

Terminal.Write("--- unions ---");
Describe(Value.Nothing);
Describe(Value.Number(42));
Describe(Value.Flag(1));

Terminal.Write("--- a union prints its active variant ---");
Variable v: Value = Value.Number(7);
Terminal.Write(v);

Terminal.Write("--- structs copy by value ---");
Variable a: Point = Point { x: 1, y: 2 };
Variable b: Point = a;
b.x = 99;
Terminal.Write(a);
Terminal.Write(b);