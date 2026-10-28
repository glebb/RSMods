using System.Runtime.CompilerServices;
using RSMods.Data;

namespace RSMods.Core.Tests;

internal static class TestDataDirectory
{
    // RSMods' own files (settings, work folders) go to the test's build folder, never the user's: on macOS they
    // would otherwise be in ~/Library/Application Support/RSMods
    [ModuleInitializer]
    internal static void Initialize() => Constants.DataDirectory = AppContext.BaseDirectory;
}
