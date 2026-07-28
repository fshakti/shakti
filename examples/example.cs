// C# subset examples runnable by `./shakti example.cs`.

using System;
using System.Collections.Generic;

int Square(int x) => x * x;

int Clamp(int x, int low = 0, int high = 10) {
    if (x < low) {
        return low;
    } else if (x > high) {
        return high;
    }
    return x;
}

// Arithmetic, defaults, keyword arguments, and a one-argument lambda.
var squared = Square(6);
Console.WriteLine($"square {squared}");
var clamped = Clamp(15, high: 12);
Console.WriteLine($"clamp {clamped}");
Func<int, int> increment = x => x + 1;
var incremented = increment(9);
Console.WriteLine($"lambda {incremented}");

// Arrays, ranges, dictionaries, loops, and augmented assignment.
var nums = new[] { 2, 4, 6, 8 };
var size = nums.Length;
var window = nums[1..3];
Console.WriteLine($"slice {window}");
var info = new Dictionary<string, object> { { "name", "shakti" }, { "count", size } };
var who = info["name"];
var howMany = info["count"];
Console.WriteLine($"dict {who} {howMany}");

var total = 0;
for (int i = 0; i < nums.Length; i++) {
    total += nums[i];
}
Console.WriteLine($"for {total}");

var countdown = 3;
while (countdown > 0) {
    Console.WriteLine($"while {countdown}");
    countdown -= 1;
}

var name = "csharp";
Console.WriteLine($"f-string {name}:{size}");

// Numeric arrays and tables lowered to native Shakti vectors and tables.
var data = Np.Array(new[] { 1, 2, 3, 4 });
var scaled = data * 2;
var frame = Pd.DataFrame(new { value = data, scaled = scaled });
Console.WriteLine(frame);
var checksum = Np.Sum(frame["scaled"]);
Console.WriteLine($"sum {checksum}");
