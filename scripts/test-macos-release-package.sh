#!/usr/bin/env bash
set -euo pipefail

scripts=$(cd "$(dirname "$0")" && pwd)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/scoreboard-pkg-test.XXXXXXXX")
trap 'rm -rf "$scratch"' EXIT

# Compile a Mach-O bundle without an OBS SDK. Use the actual packaging and
# signature-verification tools, stubbing operations that require credentials.
printf 'int obs_module_load(void) { return 1; }\n' |
  /usr/bin/clang -x c -bundle -o "$scratch/fixture.so" -
printf '{"name":"streamn-obs-scoreboard","version":"1.2.3"}\n' > "$scratch/manifest.json"
mkdir "$scratch/bin"
cat > "$scratch/bin/mock-apple-tool" <<'MOCK'
#!/bin/bash
set -euo pipefail
case "${0##*/}:$1" in
  codesign:--force)
    [[ "$*" == *'--options runtime --timestamp --sign test-application --keychain test-keychain'* ]]
    exec /usr/bin/codesign --force --sign - "${@: -1}"
    ;;
  productbuild:--distribution)
    [[ "$*" == *'--timestamp --sign test-installer --keychain test-keychain'* ]]
    exec /usr/bin/productbuild "${@:1:4}" "${@: -1}"
    ;;
  pkgutil:--check-signature)
    [[ -s "$2" ]]
    ;;
  pkgutil:*) exec /usr/sbin/pkgutil "$@" ;;
  xcrun:notarytool)
    if [[ "$2" == log ]]; then
      [[ "$3" == test-submission ]]
      echo 'Mocked Apple rejection details.'
      exit 0
    fi
    [[ "$2" == submit && -f "$3" && "$*" == *'--wait --timeout 45m --output-format plist'* ]]
    cat <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
  <key>id</key><string>test-submission</string>
  <key>status</key><string>${SCOREBOARD_TEST_NOTARY_STATUS}</string>
</dict></plist>
EOF
    exit "${SCOREBOARD_TEST_SUBMIT_EXIT:-0}"
    ;;
  xcrun:stapler)
    [[ -f "$3" ]]
    case "$2" in
      staple) touch "$3.test-ticket" ;;
      validate) [[ -f "$3.test-ticket" && "${SCOREBOARD_TEST_STAPLE_VALID:-1}" == 1 ]] ;;
      *) exit 1 ;;
    esac
    ;;
  *) exec "/usr/bin/${0##*/}" "$@" ;;
esac
MOCK
chmod +x "$scratch/bin/mock-apple-tool"
for name in codesign productbuild pkgutil xcrun; do
  ln -s mock-apple-tool "$scratch/bin/$name"
done

export MACOS_SIGNING_IDENTITY=test-application MACOS_INSTALLER_SIGNING_IDENTITY=test-installer
export MACOS_SIGNING_KEYCHAIN=test-keychain MACOS_NOTARIZE_APPLE_ID=test-apple-id
export MACOS_NOTARIZE_PASSWORD=test-notary-password MACOS_NOTARIZE_TEAM_ID=test-team-id

package() {
  PATH="$scratch/bin:$PATH" bash "$scripts/package-macos-release.sh" \
    1.2.3 "$scratch/fixture.so" "$scratch/manifest.json" "$1"
}

SCOREBOARD_TEST_NOTARY_STATUS=Accepted package "$scratch/accepted.pkg"
/usr/sbin/installer -dominfo -pkg "$scratch/accepted.pkg" | grep -Fx CurrentUserHomeDirectory
/usr/sbin/pkgutil --expand-full "$scratch/accepted.pkg" "$scratch/unpacked"
component="$scratch/unpacked/scoreboard-payload.pkg"
bundle="$component/Payload/streamn-obs-scoreboard.plugin"
grep -F 'install-location="/Library/Application Support/obs-studio/plugins"' "$component/PackageInfo"
grep -F 'relocatable="false"' "$component/PackageInfo"
cmp "$scratch/manifest.json" "$bundle/Contents/Resources/manifest.json"
[[ $(/usr/bin/plutil -extract CFBundleShortVersionString raw -o - "$bundle/Contents/Info.plist") == 1.2.3 ]]
/usr/bin/codesign --verify --strict "$bundle"

if SCOREBOARD_TEST_NOTARY_STATUS=Invalid SCOREBOARD_TEST_SUBMIT_EXIT=1 \
    package "$scratch/rejected.pkg" > "$scratch/rejected.log" 2>&1; then
  echo 'A rejected notarization unexpectedly succeeded.' >&2
  exit 1
fi
grep -F 'Apple did not accept the package.' "$scratch/rejected.log"
grep -F 'Mocked Apple rejection details.' "$scratch/rejected.log"
[[ ! -f "$scratch/rejected.pkg" ]]

if SCOREBOARD_TEST_NOTARY_STATUS=Accepted SCOREBOARD_TEST_STAPLE_VALID=0 \
    package "$scratch/invalid-ticket.pkg" > "$scratch/invalid-ticket.log" 2>&1; then
  echo 'An invalid notarization ticket unexpectedly succeeded.' >&2
  exit 1
fi
[[ ! -f "$scratch/invalid-ticket.pkg" ]]

echo 'macOS release packaging integration test passed.'
