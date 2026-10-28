using System;
using System.IO;
using RSMods.Core;

namespace RSMods.Data
{
    public static class Constants
    {
        // Where RSMods keeps its own files: the executable's own directory (replaces Application.StartupPath /
        // Application.ExecutablePath). On macOS the app bundle isn't writable when downloaded and is replaced by an
        // update, so there it is ~/Library/Application Support/RSMods.
        // Tests set it to a folder of their own.
        public static string DataDirectory
        {
            get => _dataDirectory ??= OperatingSystem.IsWindows() ? AppContext.BaseDirectory : MacLocations.DataFolder;
            set => _dataDirectory = value;
        }
        private static string _dataDirectory;

        public static string RSFolder { get; set; }
        public static string SavePath { get; set; }

        public static bool SavePathDeclined { get; set; }

        public static void SaveBaseSettings()
        {
            FlatKeyValueSettingsStore.UpdateFile(SettingsPath, settings =>
            {
                settings.SetString("RSPath", RSFolder);
                settings.SetString("SavePath", SavePath);
                settings.SetString("BypassSavePrompt", SavePathDeclined.ToString());
            });
        }

        public static string SettingsPath { get { return Path.Combine(DataDirectory, "GUI_Settings.ini"); } }
        public static string TwitchTokenPath { get { return Path.Combine(DataDirectory, "TwitchAuth.dat"); } }
        public static string TwitchRewardsPath { get { return Path.Combine(DataDirectory, "TwitchEnabledEffects.xml"); } }
        public static string CachePsarcPath { get { return Path.Combine(RSFolder, "cache.psarc"); } }
        public static string WorkFolder { get { return Path.Combine(DataDirectory, "Temp"); } }
        public static string CachePcPath { get { return Path.Combine(WorkFolder, "cache_psarc_RS2014_Pc"); } }
        public static string Cache3_7zPath { get { return Path.Combine(CachePcPath, "cache3.7z"); } }
        public static string Cache4_7zPath { get { return Path.Combine(CachePcPath, "cache4.7z"); } }
        public static string Cache7_7zPath { get { return Path.Combine(CachePcPath, "cache7.7z"); } }
        public static string WwiseInitBnk_InternalPath { get { return Path.Combine("audio", "windows", "init.bnk"); } }
        public static string WwiseInitBnk_CustomPath { get { return Path.Combine(CustomModsFolder, "init.bnk"); } }
        public static string LocalizationCSV_InternalPath { get { return Path.Combine("localization", "maingame.csv"); } }
        public static string TuningsJSON_InternalPath { get { return Path.Combine("manifests", "tuning.database.json"); } }
        public static string CustomModsFolder { get { return Path.Combine(DataDirectory, "CustomMods"); } }
        public static string LocalizationCSV_CustomPath { get { return Path.Combine(CustomModsFolder, "maingame.csv"); } }
        public static string TuningJSON_CustomPath { get { return Path.Combine(CustomModsFolder, "tuning.database.json"); } }
        public static string IntroGFX_InternalPath { get { return Path.Combine("gfxassets", "views", "introsequence.gfx"); } }
        public static string IntroGFX_CustomPath { get { return Path.Combine(CustomModsFolder, "introsequence.gfx"); } }
        public static string IntroGFX_MaxPath { get { return Path.Combine(CustomModsFolder, "introsequence_max.gfx"); } }
        public static string IntroGFX_MidPath { get { return Path.Combine(CustomModsFolder, "introsequence_mid.gfx"); } }
        public static string CacheBackupPath { get { return Path.Combine(RSFolder, "cache.bak"); } }
        public static string ExtendedMenuJson_CustomPath { get { return Path.Combine(CustomModsFolder, "ui_menu_pillar_mission.database.json"); } }
        public static string ExtendedMenuJson_InternalPath { get { return Path.Combine("manifests", "ui_menu_pillar_mission.database.json"); } }
        public static string DirectConnectStartupJson_CustomPath { get { return Path.Combine(CustomModsFolder, "ui_menu_pillar_startup.database.json"); } }
        public static string DirectConnectStartupJson_InternalPath { get { return Path.Combine("manifests", "ui_menu_pillar_startup.database.json"); } }
        public static string MainMenuJson_CustomPath { get { return Path.Combine(CustomModsFolder, "ui_menu_pillar_main.database.json"); } }
        public static string MainMenuJson_InternalPath { get { return Path.Combine("manifests", "ui_menu_pillar_main.database.json"); } }
        public static string ToneManager_InternalPath { get { return Path.Combine("manifests", "tonemanager.database.json"); } }
        public static string ToneManager_CustomPath { get { return Path.Combine(CustomModsFolder, "tonemanager.database.json"); } }
    }
}
