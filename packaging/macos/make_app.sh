#!/bin/sh
# Wrap a built OpenSc3k executable into OpenSc3k.app with its Dock icon.
#   packaging/macos/make_app.sh PATH/TO/OpenSc3k OUTPUT_DIRECTORY
set -e
exe="$1"; out="$2"
here="$(cd "$(dirname "$0")" && pwd)"
app="$out/OpenSc3k.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$here/Info.plist" "$app/Contents/Info.plist"
cp "$here/AppIcon.icns" "$app/Contents/Resources/AppIcon.icns"
cp "$exe" "$app/Contents/MacOS/OpenSc3k-bin"
# Finder starts apps without arguments; run windowed by default and keep any arguments the caller supplies.
cat > "$app/Contents/MacOS/OpenSc3k" <<'EOF'
#!/bin/sh
exec "$(dirname "$0")/OpenSc3k-bin" -w "$@"
EOF
chmod +x "$app/Contents/MacOS/OpenSc3k" "$app/Contents/MacOS/OpenSc3k-bin"
# Ad-hoc sign so the bundle launches on Apple silicon; replace with a Developer ID identity for distribution.
codesign --force --deep --sign - "$app" >/dev/null 2>&1 || true
echo "$app"
