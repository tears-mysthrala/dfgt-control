using System.Text.Json;
using DfgtControl;

if (args.Length != 1)
    return 2;
var options = new JsonSerializerOptions
{
    PropertyNameCaseInsensitive = true
};
var corpus = JsonSerializer.Deserialize<GoldenCorpus>(
    File.ReadAllText(args[0]), options);
if (corpus?.Cases is null || corpus.Cases.Count == 0)
    return 3;
foreach (var test in corpus.Cases)
{
    var actual = ProfileValidator.IsValid(test.Document, out var error);
    if (actual != test.Valid)
    {
        Console.Error.WriteLine(
            $"{test.Name}: expected {test.Valid}, got {actual}: {error}");
        return 1;
    }
}
return 0;

internal sealed class GoldenCorpus
{
    public List<GoldenCase> Cases { get; set; } = [];
}

internal sealed class GoldenCase
{
    public string Name { get; set; } = "";
    public bool Valid { get; set; }
    public ProfileDocument? Document { get; set; }
}
