namespace RSMods.Core.Tests;

/// <summary>The configurator installs, updates and removes the Mac mods library in RS Re-ARMed's mods folder.</summary>
public sealed class MacModsLibraryTests : IDisposable
{
    private readonly TemporaryDirectory _home = new();
    private bool _gameRunning;

    public void Dispose() => _home.Dispose();

    private string Bundled => _home.File("bundled.dylib");

    private MacModsLibrary Library() => new(_home.Path, Bundled, () => _gameRunning);

    private void AddGame(bool supportsMods = true)
    {
        string macOS = Path.Combine(_home.Path, "game", "Rocksmith2014.app", "Contents", "MacOS");
        Directory.CreateDirectory(macOS);
        File.WriteAllText(Path.Combine(macOS, "Rocksmith2014"), supportsMods ? "\0_rc_hook_add\0" : "\0_main\0");
    }

    private static void WriteLibrary(string path, string version) =>
        File.WriteAllBytes(path, [1, 2, 0, .. "rearmed-mods-version:"u8, .. System.Text.Encoding.ASCII.GetBytes(version), 0, 9]);

    [Fact]
    public void WithoutTheGameThereIsNoRearmed()
    {
        WriteLibrary(Bundled, "0.1.0");

        Assert.Equal(MacModsState.NoRearmed, Library().GetState());
    }

    [Fact]
    public void AGameWithoutTheHookApiIsTooOld()
    {
        AddGame(supportsMods: false);
        WriteLibrary(Bundled, "0.1.0");

        Assert.Equal(MacModsState.RearmedTooOld, Library().GetState());
    }

    [Fact]
    public void TheHookApiIsFoundAcrossTheReadChunks()
    {
        AddGame();
        string game = Path.Combine(_home.Path, "game", "Rocksmith2014.app", "Contents", "MacOS", "Rocksmith2014");
        // The symbol straddles the first 1 MB chunk's end
        File.WriteAllBytes(game, [.. new byte[(1 << 20) - 5], .. "_rc_hook_add"u8, .. new byte[100]]);
        WriteLibrary(Bundled, "0.1.0");

        Assert.Equal(MacModsState.NotInstalled, Library().GetState());
    }

    [Fact]
    public void WithoutABundledLibraryThereIsNothingToInstall()
    {
        AddGame();

        Assert.Equal(MacModsState.NoBundledLibrary, Library().GetState());
    }

    [Fact]
    public void InstallCopiesTheBundledLibraryIntoTheModsFolder()
    {
        AddGame();
        WriteLibrary(Bundled, "0.1.0");
        MacModsLibrary library = Library();
        Assert.Equal(MacModsState.NotInstalled, library.GetState());

        library.Install();

        Assert.Equal(MacModsState.Installed, library.GetState());
        Assert.Equal(File.ReadAllBytes(Bundled), File.ReadAllBytes(_home.File(Path.Combine("mods", MacModsLibrary.FileName))));
        Assert.Equal("0.1.0", library.InstalledVersion);
        Assert.False(File.Exists(library.InstalledLibrary + ".new"));
    }

    [Fact]
    public void ADifferentInstalledLibraryCanBeUpdated()
    {
        AddGame();
        WriteLibrary(Bundled, "0.2.0");
        MacModsLibrary library = Library();
        Directory.CreateDirectory(Path.GetDirectoryName(library.InstalledLibrary)!);
        WriteLibrary(library.InstalledLibrary, "0.1.0");

        Assert.Equal(MacModsState.UpdateAvailable, library.GetState());
        Assert.Equal("0.1.0", library.InstalledVersion);
        Assert.Equal("0.2.0", library.BundledVersion);

        library.Install();

        Assert.Equal(MacModsState.Installed, library.GetState());
        Assert.Equal("0.2.0", library.InstalledVersion);
    }

    [Fact]
    public void RemoveTakesTheLibraryOut()
    {
        AddGame();
        WriteLibrary(Bundled, "0.1.0");
        MacModsLibrary library = Library();
        library.Install();

        library.Remove();

        Assert.Equal(MacModsState.NotInstalled, library.GetState());
    }

    [Fact]
    public void NothingChangesWhileTheGameRuns()
    {
        AddGame();
        WriteLibrary(Bundled, "0.1.0");
        MacModsLibrary library = Library();
        _gameRunning = true;

        Assert.Throws<InvalidOperationException>(library.Install);
        Assert.False(File.Exists(library.InstalledLibrary));

        _gameRunning = false;
        library.Install();
        _gameRunning = true;

        Assert.Throws<InvalidOperationException>(library.Remove);
        Assert.True(File.Exists(library.InstalledLibrary));
    }

    [Fact]
    public void ALibraryWithoutAVersionTagHasNoVersion()
    {
        File.WriteAllBytes(Bundled, [1, 2, 3]);

        Assert.Null(MacModsLibrary.VersionOf(Bundled));
    }

    [Fact]
    public void TheHomeIsTheGameFoldersParent()
    {
        string game = Path.Combine(_home.Path, "game");

        Assert.Equal(_home.Path, MacModsLibrary.ForGameFolder(game).Home);
        Assert.Equal(_home.Path, MacModsLibrary.ForGameFolder(game + Path.DirectorySeparatorChar).Home);
    }
}
