using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using Avalonia.Threading;
using RSMods.Data;

namespace RSMods.Services;

/// <summary>
/// The last line of defence the old WinForms Program had (a try/catch with a message box): an error nothing else
/// handled is written to a crash log and shown, instead of the app vanishing without a word.
/// </summary>
internal static class CrashReporter
{
    private const string LogFileName = "RSMods_crash.log";
    private const uint MB_ICONERROR = 0x10;

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int MessageBoxW(IntPtr hWnd, string text, string caption, uint type);

    /// <summary>Hooks background threads and unobserved tasks. Call once, before the app starts.</summary>
    public static void Install()
    {
        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
        {
            if (e.ExceptionObject is Exception ex)
                Report(ex, fatal: e.IsTerminating);
        };

        // A failed fire-and-forget task only gets logged; showing it could pop up long after whatever caused it.
        TaskScheduler.UnobservedTaskException += (_, e) =>
        {
            WriteLog(e.Exception);
            e.SetObserved();
        };
    }

    /// <summary>
    /// Hooks the UI thread. Only once Avalonia is set up: touching Dispatcher.UIThread earlier makes it settle on a
    /// placeholder dispatcher that can't run the main loop, and the app fails to start.
    /// </summary>
    public static void InstallUiThread()
    {
        // Errors on the UI thread (button handlers, async void) are shown and the app carries on, as WinForms did.
        Dispatcher.UIThread.UnhandledException += (_, e) =>
        {
            Report(e.Exception, fatal: false);
            e.Handled = true;
        };
    }

    public static void Report(Exception exception, bool fatal)
    {
        string? log = WriteLog(exception);
        string text = (fatal ? "RSMods ran into an error and has to close:" : "RSMods ran into an error:")
            + $"\n\n{exception.Message}"
            + (log is null ? string.Empty : $"\n\nThe details were saved to:\n{log}");

        // A native box needs no running Avalonia, so it also works when startup itself failed
        if (OperatingSystem.IsWindows())
        {
            MessageBoxW(IntPtr.Zero, text, "RSMods error", MB_ICONERROR);
            return;
        }
        Console.Error.WriteLine(text);
        if (OperatingSystem.IsMacOS())
            ShowMacAlert(text);
    }

    // macOS' counterpart of the message box: an alert from osascript, which also needs no running Avalonia. Like the
    // message box, it waits until it is closed. The text is passed as an argument, so it needs no quoting.
    private static void ShowMacAlert(string text)
    {
        try
        {
            using Process? osascript = Process.Start(new ProcessStartInfo("/usr/bin/osascript")
            {
                ArgumentList =
                {
                    "-e", "on run argv",
                    "-e", "display alert \"RSMods error\" message (item 1 of argv) as critical",
                    "-e", "end run",
                    text,
                },
                UseShellExecute = false,
            });
            osascript?.WaitForExit();
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException)
        {
            // The console and the log still have it
        }
    }

    private static string? WriteLog(Exception exception)
    {
        string entry = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {exception}{Environment.NewLine}{Environment.NewLine}";
        // With RSMods' own files first (next to RSMods on Windows); the temp folder if that one isn't writable.
        foreach (string folder in new[] { Constants.DataDirectory, Path.GetTempPath() })
        {
            try
            {
                string path = Path.Combine(folder, LogFileName);
                File.AppendAllText(path, entry);
                return path;
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
            }
        }

        return null;
    }
}
