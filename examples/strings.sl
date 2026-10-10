// examples/strings.sl -- String: bytes whose length is a runtime value.
//
// Run with: siyana run examples/strings.sl
//
// A String is not `[N]Character`. An array's length is fixed by its type, so
// it is known before any value exists; a String's length is whatever turned
// out to be there. That is the whole distinction, and it is what lets a path,
// a command line, or the result of a read exist at all. See decision 27.

Terminal.Write("--- declaring ---");

Variable path: String = "/usr/local/bin";
Terminal.Write(path);
Terminal.Write(path.Length());

Terminal.Write("--- indexing gives a byte ---");

// A Character, not a String of length 1. The byte is what a byte buffer is
// made of, and what `path[i] == '/'` tests a path with.
Variable i: Word = 0;
Loop i < path.Length() {
    Terminal.Write(path[i]);
    i = i + 1;
}

Terminal.Write("--- concatenating ---");

// The result is a String, not a print. This is how a path is assembled from
// pieces whose lengths are not known in advance.
Variable root: String = "/etc";
Variable leaf: String = "/passwd";
Variable whole: String = root + leaf;
Terminal.Write(whole);

Terminal.Write("--- building one byte at a time ---");

// What a fixed-size array cannot do: the length is not known until the loop
// has run. Repeated concatenation is quadratic, and that is known too.
Variable spaced: String = "";
i = 0;
Loop i < 4 {
    spaced = spaced + "ab";
    i = i + 1;
}
Terminal.Write(spaced.Length());
Terminal.Write(spaced);

Terminal.Write("--- bytes in, String out ---");

// The bridge an OS needs. A file arrives as bytes; a file path is a String.
// Going the other way is not offered, because an array's length is fixed by
// its declaration and there is no `[s.Length()]` to write.
Variable raw: [5]Character = [47, 98, 111, 111, 116];
Variable name: String = raw.Text();
Terminal.Write(name);

Terminal.Write("--- passing to a function ---");

Function WithExtension(text: String, suffix: String) -> String {
    Return text + suffix;
}

Terminal.Write(WithExtension("/tmp/report", ".tmp"));

Terminal.Write("--- bounds are checked ---");

// Same rule as an array, because it is the same mistake: an index that runs
// off the end of the thing being indexed.
//
// Terminal.Write(path[99]);

Terminal.Write("--- and a Character does not become a String ---");

// A byte is not a byte sequence. If it were, `s[0]` and a one-character String
// would be the same value, and they are not.
//
// Variable one: String = 'a';