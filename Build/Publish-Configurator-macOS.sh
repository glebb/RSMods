#!/bin/sh
# Produces RSMods.app, the configurator for macOS (Apple Silicon), the counterpart of Publish-Configurator.ps1:
#   Build/Publish-Configurator-macOS.sh         -> Build/Artifacts/configurator/osx-arm64/RSMods.app
#   Build/Publish-Configurator-macOS.sh --zip   also RSMods-<version>-macOS.zip beside it, the release download
# Self-contained, like the Windows publish: the Mac needs no .NET runtime. The app carries the Mac mods library
# (Mac/build.sh, run by the build), which its Home tab installs into RS Re-ARMed. It is ad-hoc signed, not
# notarized: the first start needs Open Anyway in System Settings > Privacy & Security.
# Needs the .NET 10 SDK and the Xcode command line tools. Only removes its own output directory.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/Build/Artifacts/configurator/osx-arm64
app=$out/RSMods.app
version=$(sed -n 's:.*<Version>\(.*\)</Version>.*:\1:p' "$root/Directory.Build.props")

rm -rf "$out"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
echo "Publishing the configurator (osx-arm64, self-contained, Release)..."
dotnet publish "$root/GUI.Avalonia/GUI.Avalonia.csproj" --configuration Release --runtime osx-arm64 --self-contained \
  -p:DebugType=none -p:DebugSymbols=false --output "$app/Contents/MacOS" --nologo
# A referenced project (GUI.Core) still emits a .pdb that publish copies
find "$app/Contents/MacOS" -name '*.pdb' -delete

# Fails if the executable or a required runtime asset is missing
missing=0
for f in RSMods RSMods.dll rearmed-mods.dylib Rocksmith2014.PSARC.dll Rocksmith2014.Common.dll \
  Rocksmith2014.Audio.dll FSharp.Core.dll; do
  [ -f "$app/Contents/MacOS/$f" ] || { echo "missing: $f" >&2; missing=1; }
done
[ $missing = 0 ] || { echo "Publish output is incomplete." >&2; exit 1; }

cp "$root/GUI.Avalonia/Assets/RSMods.icns" "$app/Contents/Resources/"
cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>RSMods</string>
    <key>CFBundleDisplayName</key><string>RSMods</string>
    <key>CFBundleIdentifier</key><string>io.github.lovrom8.rsmods</string>
    <key>CFBundleExecutable</key><string>RSMods</string>
    <key>CFBundleIconFile</key><string>RSMods</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$version</string>
    <key>CFBundleVersion</key><string>$version</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
codesign --force --deep --sign - "$app"
echo "Built $app ($version)"

if [ "${1:-}" = --zip ]; then
  zip=$out/RSMods-$version-macOS.zip
  ditto -c -k --keepParent "$app" "$zip"  # ditto, not zip: keeps the signature and the bundle's attributes
  echo "Release: $zip"
fi
