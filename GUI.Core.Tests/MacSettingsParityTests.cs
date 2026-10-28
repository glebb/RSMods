using System.Text.Json;
using System.Text.RegularExpressions;

namespace RSMods.Core.Tests;

/// <summary>
/// Guards the configurator/Mac library contract: every setting the Mac mods read from <c>RSMods.ini</c> is one the
/// configurator writes (<c>mods.manifest.json</c>), in the same section and with the same default.
/// <para>
/// The Mac library (<c>Mac/src</c>) shares no code with the DLL, only <c>RSMods.ini</c>. A key renamed or moved on
/// the Windows side leaves the Mac mod reading the old one, silently: this test fails on any system instead.
/// </para>
/// </summary>
public sealed class MacSettingsParityTests
{
    private static readonly string IniSource =
        File.ReadAllText(Path.Combine(SettingsKeyParityTests.FindRepositoryRoot(), "Mac", "src", "rsmods_ini.c"));

    public static TheoryData<string, string, string> MacDefaults()
    {
        var data = new TheoryData<string, string, string>();
        foreach (var ((section, key), value) in ReadDefaults())
            data.Add(section, key, value);
        return data;
    }

    [Theory]
    [MemberData(nameof(MacDefaults))]
    public void EveryMacSettingIsInTheManifest(string section, string key, string macDefault)
    {
        Dictionary<(string, string), string> manifest = ReadManifestDefaults();

        Assert.True(manifest.TryGetValue((section, key), out string? manifestDefault),
            $"The Mac library reads [{section}] {key}, which mods.manifest.json doesn't have. If it was renamed or moved, " +
            "change Mac/src/rsmods_ini.c (DEFAULT_INI, DEFAULTS) and the mod in Mac/src to match.");
        Assert.True(string.Equals(macDefault, manifestDefault, StringComparison.OrdinalIgnoreCase),
            $"[{section}] {key} defaults to \"{macDefault}\" on the Mac and \"{manifestDefault}\" in mods.manifest.json.");
    }

    // The file the game writes when there is none (DEFAULT_INI) and the values used where a key is missing
    // (DEFAULTS) must agree
    [Fact]
    public void TheWrittenIniHasTheDefaults()
    {
        Assert.Equal(ReadDefaults(), ReadDefaultIni());
    }

    // DEFAULTS: {"Section", "Key", "value"},
    private static Dictionary<(string Section, string Key), string> ReadDefaults()
    {
        string table = Regex.Match(IniSource, @"\}\s*DEFAULTS\[\]\s*=\s*\{(?<body>.*?)\n\};", RegexOptions.Singleline)
            .Groups["body"].Value;
        var defaults = new Dictionary<(string, string), string>();
        foreach (Match m in Regex.Matches(table, @"\{""(?<section>[^""]+)"",\s*""(?<key>[^""]+)"",\s*""(?<value>[^""]*)""\}"))
            defaults.Add((m.Groups["section"].Value, m.Groups["key"].Value), m.Groups["value"].Value);
        Assert.NotEmpty(defaults);
        return defaults;
    }

    // DEFAULT_INI: "[Section]\n" and "Key = value\n" string literals
    private static Dictionary<(string Section, string Key), string> ReadDefaultIni()
    {
        string body = Regex.Match(IniSource, @"DEFAULT_INI\[\]\s*=(?<body>.*?);\n", RegexOptions.Singleline)
            .Groups["body"].Value;
        var values = new Dictionary<(string, string), string>();
        string section = "";
        foreach (Match m in Regex.Matches(body, @"""(?<line>[^""]*)\\n"""))
        {
            string line = m.Groups["line"].Value.Trim();
            if (line.StartsWith('[') && line.EndsWith(']'))
                section = line[1..^1];
            else if (line.Length > 0 && !line.StartsWith(';') && line.Contains('='))
                values.Add((section, line[..line.IndexOf('=')].Trim()), line[(line.IndexOf('=') + 1)..].Trim());
        }
        return values;
    }

    private static Dictionary<(string Section, string Key), string> ReadManifestDefaults()
    {
        string path = Path.Combine(SettingsKeyParityTests.FindRepositoryRoot(), "mods.manifest.json");
        using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(path));
        var defaults = new Dictionary<(string, string), string>();
        foreach (JsonElement element in doc.RootElement.EnumerateArray())
        {
            if (!element.TryGetProperty("ini", out JsonElement ini) || ini.ValueKind != JsonValueKind.Object)
                continue;
            string? section = ini.GetProperty("section").GetString();
            string? name = ini.GetProperty("name").GetString();
            if (string.IsNullOrEmpty(section) || string.IsNullOrEmpty(name))
                continue;
            defaults[(section, name)] =
                element.TryGetProperty("default", out JsonElement d) && d.ValueKind == JsonValueKind.String ? d.GetString()! : "";
        }
        return defaults;
    }
}
