var data = Np.Array(new[] { 1, 2, 3, 4 });
var scaled = data * 2;
var frame = Pd.DataFrame(new { value = data, scaled = scaled });

Console.WriteLine(frame);
Console.WriteLine(Np.Sum(frame["scaled"]));
