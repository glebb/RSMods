#nullable enable
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace RSMods
{
    public enum MacModsState
    {
        /// <summary>No RS Re-ARMed game in the folder.</summary>
        NoRearmed,
        /// <summary>RS Re-ARMed before 1.2.0: it loads no mods.</summary>
        RearmedTooOld,
        /// <summary>The configurator was built without the library (not on macOS, or Mac/build.sh failed).</summary>
        NoBundledLibrary,
        NotInstalled,
        /// <summary>The installed library is the one the configurator carries.</summary>
        Installed,
        /// <summary>Another library is installed: older, newer or a local build.</summary>
        UpdateAvailable,
    }

    /// <summary>
    /// Installs the Mac mods library the configurator carries (rearmed-mods.dylib beside it, built by Mac/build.sh)
    /// into RS Re-ARMed's mods folder, which RS Re-ARMed 1.2.0 and later load at startup.
    /// </summary>
    public sealed class MacModsLibrary
    {
        public const string FileName = "rearmed-mods.dylib";

        // rsm_version_tag in Mac/src/rsmods.c
        private static readonly byte[] VersionMarker = Encoding.ASCII.GetBytes("rearmed-mods-version:");
        // RS Re-ARMed 1.2.0 and later export the hook API to mods
        private static readonly byte[] HookApiSymbol = Encoding.ASCII.GetBytes("_rc_hook_add");

        private readonly Func<bool> _gameRunning;

        /// <param name="home">RS Re-ARMed's folder, the parent of its game folder.</param>
        /// <param name="bundledLibrary">The library to install.</param>
        /// <param name="gameRunning">Whether the game is running; pgrep by default.</param>
        public MacModsLibrary(string home, string bundledLibrary, Func<bool>? gameRunning = null)
        {
            Home = home;
            BundledLibrary = bundledLibrary;
            _gameRunning = gameRunning ?? GameRunning;
        }

        /// <summary>For RS Re-ARMed's folder around the chosen game folder, with the library beside the configurator.</summary>
        public static MacModsLibrary ForGameFolder(string gameFolder) =>
            new MacModsLibrary(
                Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(gameFolder)) ?? MacLocations.Home,
                Path.Combine(AppContext.BaseDirectory, FileName));

        public string Home { get; }

        public string BundledLibrary { get; }

        public string InstalledLibrary => Path.Combine(Home, "mods", FileName);

        private string GameExecutable => Path.Combine(Home, "game", "Rocksmith2014.app", "Contents", "MacOS", "Rocksmith2014");

        public string? BundledVersion => File.Exists(BundledLibrary) ? VersionOf(BundledLibrary) : null;

        public string? InstalledVersion => File.Exists(InstalledLibrary) ? VersionOf(InstalledLibrary) : null;

        public MacModsState GetState()
        {
            if (!File.Exists(GameExecutable))
                return MacModsState.NoRearmed;
            if (!Contains(GameExecutable, HookApiSymbol))
                return MacModsState.RearmedTooOld;
            if (!File.Exists(BundledLibrary))
                return MacModsState.NoBundledLibrary;
            if (!File.Exists(InstalledLibrary))
                return MacModsState.NotInstalled;
            return SameContents(InstalledLibrary, BundledLibrary) ? MacModsState.Installed : MacModsState.UpdateAvailable;
        }

        /// <summary>Installs or updates the library. Throws when the game is running or the copy fails.</summary>
        public void Install()
        {
            ThrowIfGameRunning();
            string mods = Path.GetDirectoryName(InstalledLibrary)!;
            Directory.CreateDirectory(mods);
            string temp = InstalledLibrary + ".new";
            File.Copy(BundledLibrary, temp, overwrite: true);
            // A file copied out of a downloaded app keeps its quarantine flag, and the game would not load it
            if (OperatingSystem.IsMacOS())
                removexattr(temp, "com.apple.quarantine", 0);
            // A new file renamed over the old one: overwriting a library in place leaves macOS with the old code
            // signature cached for the file
            File.Move(temp, InstalledLibrary, overwrite: true);
        }

        /// <summary>Removes the library; RS Re-ARMed then starts without the mods.</summary>
        public void Remove()
        {
            ThrowIfGameRunning();
            File.Delete(InstalledLibrary);
        }

        private void ThrowIfGameRunning()
        {
            if (_gameRunning())
                throw new InvalidOperationException("Quit RS Re-ARMed first.");
        }

        /// <summary>The version tag the library carries, or null.</summary>
        public static string? VersionOf(string library)
        {
            byte[] data = File.ReadAllBytes(library);
            int at = data.AsSpan().IndexOf(VersionMarker);
            if (at < 0)
                return null;
            ReadOnlySpan<byte> tail = data.AsSpan(at + VersionMarker.Length);
            int end = tail.IndexOf((byte)0);
            return Encoding.ASCII.GetString(end < 0 ? tail : tail[..end]);
        }

        private static bool Contains(string path, byte[] bytes)
        {
            // The game is ~100 MB: read in chunks, each starting with the previous one's tail so a match across the
            // boundary is found
            try
            {
                using FileStream stream = File.OpenRead(path);
                byte[] buffer = new byte[1 << 20];
                int kept = 0;
                int read;
                while ((read = stream.Read(buffer, kept, buffer.Length - kept)) > 0)
                {
                    int length = kept + read;
                    if (buffer.AsSpan(0, length).IndexOf(bytes) >= 0)
                        return true;
                    kept = Math.Min(bytes.Length - 1, length);
                    Array.Copy(buffer, length - kept, buffer, 0, kept);
                }
                return false;
            }
            catch (IOException)
            {
                return false;
            }
            catch (UnauthorizedAccessException)
            {
                return false;
            }
        }

        private static bool SameContents(string a, string b) =>
            File.ReadAllBytes(a).AsSpan().SequenceEqual(File.ReadAllBytes(b));

        private static bool GameRunning()
        {
            try
            {
                using Process pgrep = Process.Start(new ProcessStartInfo("/usr/bin/pgrep", "-x Rocksmith2014")
                {
                    RedirectStandardOutput = true,
                    UseShellExecute = false,
                })!;
                pgrep.WaitForExit();
                return pgrep.ExitCode == 0;
            }
            catch (Exception)
            {
                return false;
            }
        }

        [DllImport("libc", SetLastError = true)]
        private static extern int removexattr(string path, string name, int options);
    }
}
