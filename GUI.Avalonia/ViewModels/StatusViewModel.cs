using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RSMods.Core;
using RSMods.Core.Update;
using RSMods.Data;
using RSMods.Services;

namespace RSMods.ViewModels;

/// <summary>
/// The default landing page: startup status, the resolved Rocksmith install and save folders, on macOS the mods
/// library, and the latest GitHub release notes.
/// </summary>
internal sealed partial class StatusViewModel : ObservableObject
{
    private readonly IDialogService _dialogs;
    private readonly IGitHubReleaseService _releases;
    private readonly IAppEnvironment _environment;
    private readonly AutoSaveService _autoSave;

    public StatusViewModel(IDialogService dialogs, IGitHubReleaseService releases, IAppEnvironment environment, AutoSaveService autoSave)
    {
        _dialogs = dialogs;
        _releases = releases;
        _environment = environment;
        _autoSave = autoSave;

        Version? running = Assembly.GetExecutingAssembly().GetName().Version;
        CurrentVersion = running is null ? null : running.ToString();

        // Fetch once at launch; failures degrade to a quiet retry link rather than blocking the page.
        _ = LoadLatestReleaseAsync();
    }

    [ObservableProperty]
    private string _statusMessage = "Resolving your Rocksmith 2014 install…";

    [ObservableProperty]
    private string? _rocksmithFolder;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(SavePathDisplay))]
    private string? _savePath;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(SavePathDisplay))]
    private bool _savePathAvailable;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(SetSavePathCommand), nameof(SetRocksmithFolderCommand))]
    private bool _isReady;

    public string SavePathDisplay => SavePathAvailable ? SavePath! : "No save folder set. Profile Edits stays disabled until one is selected.";

    // ---- Mac mods library ----

    private MacModsLibrary? _macMods;

    /// <summary>On macOS the mods are a library the configurator installs into RS Re-ARMed's mods folder.</summary>
    public bool IsMac => OperatingSystem.IsMacOS();

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(MacModsTitle), nameof(MacModsDetail), nameof(CanInstallMacMods), nameof(CanRemoveMacMods), nameof(InstallMacModsText))]
    [NotifyCanExecuteChangedFor(nameof(InstallMacModsCommand), nameof(RemoveMacModsCommand))]
    private MacModsState _macModsState = MacModsState.NoRearmed;

    public string MacModsTitle => MacModsState switch
    {
        MacModsState.Installed => $"Mods installed ({_macMods?.InstalledVersion})",
        MacModsState.UpdateAvailable => "Mods update available",
        MacModsState.NotInstalled => "Mods not installed",
        _ => "Mods",
    };

    public string MacModsDetail => MacModsState switch
    {
        MacModsState.NoRearmed => "Install RS Re-ARMed first; the mods run in it. (Or change the Rocksmith folder to its game folder.)",
        MacModsState.RearmedTooOld => "Your RS Re-ARMed doesn't load mods. Install its latest version (1.2.0 or later), then come back.",
        MacModsState.NoBundledLibrary => $"This build of RSMods doesn't carry the mods library. Build it with Mac/build.sh, or copy {MacModsLibrary.FileName} into RS Re-ARMed's mods folder yourself.",
        MacModsState.NotInstalled => "Install them to use them in RS Re-ARMed.",
        MacModsState.UpdateAvailable when _macMods?.InstalledVersion == _macMods?.BundledVersion =>
            $"Installed: another build of {_macMods?.InstalledVersion}. Update to this one.",
        MacModsState.UpdateAvailable => $"Installed: {_macMods?.InstalledVersion ?? "unknown"}. Update to this version ({_macMods?.BundledVersion}).",
        _ => "Start RS Re-ARMed to use them. Settings take effect while the game runs.",
    };

    public string InstallMacModsText => MacModsState == MacModsState.UpdateAvailable ? "Update Mods" : "Install Mods";

    public bool CanInstallMacMods => MacModsState is MacModsState.NotInstalled or MacModsState.UpdateAvailable;

    public bool CanRemoveMacMods => MacModsState is MacModsState.Installed or MacModsState.UpdateAvailable;

    partial void OnRocksmithFolderChanged(string? value)
    {
        if (!IsMac || string.IsNullOrEmpty(value))
            return;
        _macMods = MacModsLibrary.ForGameFolder(value);
        RefreshMacMods();
    }

    private void RefreshMacMods()
    {
        MacModsState = _macMods?.GetState() ?? MacModsState.NoRearmed;
        // The versions in the texts can change while the state stays the same
        OnPropertyChanged(nameof(MacModsTitle));
        OnPropertyChanged(nameof(MacModsDetail));
    }

    [RelayCommand(CanExecute = nameof(CanInstallMacMods))]
    private Task InstallMacModsAsync() =>
        ChangeMacModsAsync(library => library.Install(), "Mods installed. Start RS Re-ARMed to use them.", "Install Mods");

    [RelayCommand(CanExecute = nameof(CanRemoveMacMods))]
    private Task RemoveMacModsAsync() =>
        ChangeMacModsAsync(library => library.Remove(), "Mods removed. RS Re-ARMed starts without them.", "Remove Mods");

    private async Task ChangeMacModsAsync(Action<MacModsLibrary> change, string done, string title)
    {
        if (_macMods is null)
            return;
        try
        {
            change(_macMods);
            StatusMessage = done;
        }
        catch (Exception ex) when (ex is InvalidOperationException or IOException or UnauthorizedAccessException)
        {
            await _dialogs.ShowErrorAsync(ex.Message, title);
        }
        RefreshMacMods();
    }

    // ---- Latest release ----

    [ObservableProperty]
    private bool _isLoadingRelease;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsUpToDate))]
    private bool _hasRelease;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsUpToDate))]
    private bool _updateAvailable;

    /// <summary>The running build's version, e.g. "1.2.8.4".</summary>
    [ObservableProperty]
    private string? _currentVersion;

    /// <summary>True once the release is known and it is not newer than the running build.</summary>
    public bool IsUpToDate => HasRelease && !UpdateAvailable;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasReleaseError))]
    private string? _releaseError;

    public bool HasReleaseError => !string.IsNullOrEmpty(ReleaseError);

    [ObservableProperty]
    private string? _releaseTitle;

    [ObservableProperty]
    private string? _releaseVersion;

    [ObservableProperty]
    private string? _releasePublished;

    [ObservableProperty]
    private string? _releaseNotes;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(OpenReleaseCommand))]
    private string? _releaseUrl;

    /// <summary>
    /// Points RSMods at a different Rocksmith install. Every screen has already loaded its settings from the current
    /// one, so the app restarts to load the new one's, after writing any change still waiting to save.
    /// </summary>
    [RelayCommand(CanExecute = nameof(IsReady))]
    private async Task SetRocksmithFolderAsync()
    {
        string? picked = await RSLocationResolver.ChooseRSFolderAsync(_dialogs);
        if (picked is null || string.Equals(picked, RocksmithFolder, StringComparison.OrdinalIgnoreCase))
        {
            StatusMessage = "Rocksmith folder unchanged.";
            return;
        }

        bool restart = await _dialogs.ShowChoiceAsync(
            $"RSMods will restart to load your settings from\n{picked}",
            "Change Rocksmith folder",
            "Restart",
            "Cancel");
        if (!restart)
        {
            StatusMessage = "Rocksmith folder unchanged.";
            return;
        }

        // Pending changes belong to the current folder, so they're written there before it's switched.
        if (!await _autoSave.FlushAllAsync())
        {
            StatusMessage = "Rocksmith folder unchanged.";
            await _dialogs.ShowErrorAsync(
                $"Some settings couldn't be saved, so the Rocksmith folder wasn't changed:\n{_autoSave.LastError?.Message}",
                "Change Rocksmith folder");
            return;
        }

        Constants.RSFolder = picked;
        Constants.SaveBaseSettings();

        StatusMessage = "Restarting…";
        _environment.RequestRestart();
    }

    [RelayCommand(CanExecute = nameof(IsReady))]
    private async Task SetSavePathAsync()
    {
        string? picked = await RSLocationResolver.ChangeSaveFolderAsync(_dialogs);
        if (picked is null)
        {
            StatusMessage = "Save folder unchanged.";
            return;
        }

        // Setting SavePath has the main window drop the old account's profiles and back up the new folder's
        // before profile edits unlock again. Available first: that backup is skipped while no save folder is available.
        SavePathAvailable = true;
        SavePath = picked;
        Constants.SaveBaseSettings();

        StatusMessage = "Save folder updated.";
    }

    [RelayCommand]
    private async Task LoadLatestReleaseAsync()
    {
        if (IsLoadingRelease)
            return;

        IsLoadingRelease = true;
        ReleaseError = null;
        try
        {
            ReleaseInfo release = await _releases.GetLatestReleaseAsync();

            ReleaseTitle = release.Title;
            ReleaseVersion = release.TagName;
            ReleasePublished = release.PublishedAt?.ToLocalTime().ToString("d MMM yyyy");
            ReleaseNotes = release.Notes;
            ReleaseUrl = release.HtmlUrl;
            HasRelease = true;

            // The badge shows only when the published release is strictly newer than this build; if the
            // tag has no parseable version we stay quiet rather than nagging.
            Version? running = Assembly.GetExecutingAssembly().GetName().Version;
            UpdateAvailable =
                running is not null
                && ReleaseVersioning.TryExtractVersion(release.TagName, out Version latest)
                && latest > running;
        }
        catch (Exception)
        {
            // Offline, rate-limited, or a schema change: keep the page usable, offer a retry.
            HasRelease = false;
            ReleaseError = "Couldn't reach GitHub to load the latest release.";
        }
        finally
        {
            IsLoadingRelease = false;
        }
    }

    private bool CanOpenRelease => !string.IsNullOrEmpty(ReleaseUrl);

    [RelayCommand(CanExecute = nameof(CanOpenRelease))]
    private void OpenRelease() =>
        Process.Start(new ProcessStartInfo(ReleaseUrl!) { UseShellExecute = true });
}
