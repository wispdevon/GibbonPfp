#!/usr/bin/env bash
set -euo pipefail
app_path="$1"
if [[ -z "${APPLE_CERTIFICATE_BASE64:-}" ]]; then
  codesign --force --deep --sign - "$app_path"
  echo 'No Apple certificate configured; application is ad-hoc signed, not notarized.'
  exit 0
fi
: "${APPLE_CERTIFICATE_PASSWORD:?Missing certificate password}"
: "${APPLE_SIGNING_IDENTITY:?Missing signing identity}"
signing_dir=$(mktemp -d)
signing_keychain="$signing_dir/gibbon.keychain-db"
signing_password=$(openssl rand -hex 24)
trap 'security delete-keychain "$signing_keychain" >/dev/null 2>&1 || true; rm -rf "$signing_dir"' EXIT
printf '%s' "$APPLE_CERTIFICATE_BASE64" | base64 --decode > "$signing_dir/certificate.p12"
security create-keychain -p "$signing_password" "$signing_keychain"
security unlock-keychain -p "$signing_password" "$signing_keychain"
security import "$signing_dir/certificate.p12" -k "$signing_keychain" -P "$APPLE_CERTIFICATE_PASSWORD" -T /usr/bin/codesign
security set-key-partition-list -S apple-tool:,apple: -s -k "$signing_password" "$signing_keychain" >/dev/null
while IFS= read -r -d '' binary; do
  codesign --force --timestamp --options runtime --keychain "$signing_keychain" --sign "$APPLE_SIGNING_IDENTITY" "$binary"
done < <(find "$app_path/Contents" -type f \( -name '*.dylib' -o -name '*.so' \) -print0)
codesign --force --deep --timestamp --options runtime --keychain "$signing_keychain" --sign "$APPLE_SIGNING_IDENTITY" "$app_path"
codesign --verify --deep --strict "$app_path"
