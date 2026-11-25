#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
  echo "Usage: $0 <bundle-version> <binary> <manifest.json> <output.pkg>" >&2
  exit 2
fi

# Check all credentials before writing files or contacting Apple.
missing=()
for name in MACOS_SIGNING_CERT MACOS_SIGNING_CERT_PASSWORD MACOS_SIGNING_IDENTITY \
  MACOS_INSTALLER_SIGNING_CERT MACOS_INSTALLER_SIGNING_CERT_PASSWORD \
  MACOS_INSTALLER_SIGNING_IDENTITY MACOS_NOTARIZE_APPLE_ID \
  MACOS_NOTARIZE_PASSWORD MACOS_NOTARIZE_TEAM_ID; do
  if [[ -z "${!name:-}" ]]; then
    missing+=("$name")
  fi
done
if [[ "${missing[*]:-}" ]]; then
  printf 'Missing required GitHub Actions secret: %s\n' "${missing[@]}" >&2
  exit 1
fi

script_dir="$(cd "$(dirname "$0")" && pwd)"
signing_dir="$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/scoreboard-signing.XXXXXX")"
keychain="$signing_dir/signing.keychain-db"
original_keychains=()
update_search_list=0

cleanup() {
  security delete-keychain "$keychain" 2>/dev/null || true
  if [[ "$update_search_list" == 1 ]]; then
    security list-keychains -d user -s ${original_keychains[@]+"${original_keychains[@]}"} \
      >/dev/null || true
  fi
  rm -rf -- "$signing_dir"
}
trap cleanup EXIT

keychain_password="$(uuidgen)"
keychain_list="$(security list-keychains -d user)"
while IFS='"' read -r _ existing_keychain _; do
  if [[ -n "$existing_keychain" ]]; then
    original_keychains+=("$existing_keychain")
  fi
done <<< "$keychain_list"

printf '%s' "$MACOS_SIGNING_CERT" | base64 -D > "$signing_dir/application.p12"
printf '%s' "$MACOS_INSTALLER_SIGNING_CERT" | base64 -D > "$signing_dir/installer.p12"

update_search_list=1
security create-keychain -p "$keychain_password" "$keychain"
security set-keychain-settings -lut 3600 "$keychain"
security unlock-keychain -p "$keychain_password" "$keychain"
security list-keychains -d user -s "$keychain" ${original_keychains[@]+"${original_keychains[@]}"}

# Make the same Apple intermediate available on a fresh runner.
curl --fail --silent --show-error --location --retry 3 \
  https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer \
  --output "$signing_dir/DeveloperIDG2CA.cer"
security import "$signing_dir/DeveloperIDG2CA.cer" -k "$keychain" -t cert
security import "$signing_dir/application.p12" -k "$keychain" -x -f pkcs12 \
  -P "$MACOS_SIGNING_CERT_PASSWORD" -T /usr/bin/codesign
security import "$signing_dir/installer.p12" -k "$keychain" -x -f pkcs12 \
  -P "$MACOS_INSTALLER_SIGNING_CERT_PASSWORD" -T /usr/bin/productbuild
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$keychain_password" "$keychain"

rm -- "$signing_dir/application.p12" "$signing_dir/installer.p12"
unset MACOS_SIGNING_CERT MACOS_SIGNING_CERT_PASSWORD
unset MACOS_INSTALLER_SIGNING_CERT MACOS_INSTALLER_SIGNING_CERT_PASSWORD

MACOS_SIGNING_KEYCHAIN="$keychain" bash "$script_dir/package-macos-release.sh" "$@"
