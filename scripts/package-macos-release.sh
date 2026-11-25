#!/usr/bin/env bash
set -euo pipefail

fail() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

[[ $# == 4 ]] || fail "Usage: $0 <version> <plugin binary> <manifest.json> <output.pkg>"
version=$1
binary=$2
manifest=$3
output=$4
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "Expected a numeric version (X.Y.Z)."
[[ -f "$binary" ]] || fail "Plugin binary not found: $binary"
[[ -f "$manifest" ]] || fail "Plugin manifest not found: $manifest"
[[ "$output" == *.pkg ]] || fail "Output must be a .pkg file."

for name in MACOS_SIGNING_IDENTITY MACOS_INSTALLER_SIGNING_IDENTITY \
    MACOS_NOTARIZE_APPLE_ID MACOS_NOTARIZE_PASSWORD MACOS_NOTARIZE_TEAM_ID; do
  [[ -n "${!name:-}" ]] || fail "Missing release credential: $name"
done

plugin=streamn-obs-scoreboard
identifier=com.streamndad.streamn-obs-scoreboard
install_location='/Library/Application Support/obs-studio/plugins'
scratch=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/scoreboard-pkg.XXXXXXXX")
trap 'rm -rf "$scratch"' EXIT

root="$scratch/payload"
bundle="$root/$plugin.plugin"
mkdir -p "$bundle/Contents/MacOS" "$bundle/Contents/Resources"
install -m 755 "$binary" "$bundle/Contents/MacOS/$plugin"
install -m 644 "$manifest" "$bundle/Contents/Resources/manifest.json"

cat > "$bundle/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "https://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>$plugin</string>
  <key>CFBundleIdentifier</key><string>$identifier</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>$plugin</string>
  <key>CFBundlePackageType</key><string>BNDL</string>
  <key>CFBundleShortVersionString</key><string>$version</string>
  <key>CFBundleVersion</key><string>$version</string>
</dict>
</plist>
EOF
plutil -lint "$bundle/Contents/Info.plist"

app_sign_args=(--sign "$MACOS_SIGNING_IDENTITY")
pkg_sign_args=(--sign "$MACOS_INSTALLER_SIGNING_IDENTITY")
if [[ -n "${MACOS_SIGNING_KEYCHAIN:-}" ]]; then
  app_sign_args+=(--keychain "$MACOS_SIGNING_KEYCHAIN")
  pkg_sign_args+=(--keychain "$MACOS_SIGNING_KEYCHAIN")
fi
codesign --force --options runtime --timestamp "${app_sign_args[@]}" "$bundle"
codesign --verify --strict --verbose=2 "$bundle"

# Keep the plug-in in the installer payload so Apple can inspect the actual bundle.
# Disable Installer's bundle relocation to keep the plug-in under OBS's user location.
cat > "$scratch/Component.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "https://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<array>
  <dict>
    <key>BundleIsRelocatable</key><false/>
    <key>BundleOverwriteAction</key><string>upgrade</string>
    <key>RootRelativeBundlePath</key><string>$plugin.plugin</string>
  </dict>
</array>
</plist>
EOF
pkgbuild --root "$root" --component-plist "$scratch/Component.plist" \
  --install-location "$install_location" --identifier "$identifier" \
  --version "$version" "$scratch/scoreboard-payload.pkg"

# Installer evaluates the component's location relative to the selected home domain.
cat > "$scratch/Distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Streamn Scoreboard</title>
  <options customize="never"/>
  <domains enable_anywhere="false" enable_currentUserHome="true" enable_localSystem="false"/>
  <choices-outline><line choice="scoreboard"/></choices-outline>
  <choice id="scoreboard" title="Streamn Scoreboard"><pkg-ref id="$identifier"/></choice>
  <pkg-ref id="$identifier" version="$version">scoreboard-payload.pkg</pkg-ref>
</installer-gui-script>
EOF

package="$scratch/$plugin.pkg"
productbuild --distribution "$scratch/Distribution.xml" --package-path "$scratch" \
  --timestamp "${pkg_sign_args[@]}" "$package"
pkgutil --check-signature "$package"

# Verify the package preserved every signed bundle file before sending it to Apple.
pkgutil --expand-full "$package" "$scratch/unpacked"
codesign --verify --strict --verbose=2 \
  "$scratch/unpacked/scoreboard-payload.pkg/Payload/$plugin.plugin"

notary_args=(--apple-id "$MACOS_NOTARIZE_APPLE_ID"
  --password "$MACOS_NOTARIZE_PASSWORD" --team-id "$MACOS_NOTARIZE_TEAM_ID")
notary_result="$scratch/notary-result.plist"
submit_exit=0
xcrun notarytool submit "$package" "${notary_args[@]}" \
  --wait --timeout 45m --output-format plist > "$notary_result" || submit_exit=$?
submission_id=$(plutil -extract id raw -o - "$notary_result" 2>/dev/null || true)
status=$(plutil -extract status raw -o - "$notary_result" 2>/dev/null || true)
printf 'Apple notarization result: %s (%s)\n' "${status:-Unavailable}" "${submission_id:-no submission ID}"
if [[ "$status" == Invalid || "$status" == Rejected ]]; then
  xcrun notarytool log "$submission_id" "${notary_args[@]}" || true
  fail "Apple did not accept the package."
fi
[[ "$status" == Accepted && "$submit_exit" == 0 ]] || fail "Notarization submission did not complete successfully."

xcrun stapler staple "$package"
xcrun stapler validate "$package"
pkgutil --check-signature "$package"
mv "$package" "$output"
