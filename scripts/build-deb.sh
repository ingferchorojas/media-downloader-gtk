#!/bin/sh
set -eu
build=${1:-build}
case "$build" in /*) ;; *) build="$PWD/$build" ;; esac
project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dist="$project/dist"
mkdir -p "$dist"
stage=$(mktemp -d "$dist/stage.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM
meson configure "$build" --prefix=/usr
meson compile -C "$build"
meson install -C "$build" --destdir "$stage"
mkdir -p "$stage/DEBIAN"
arch=$(dpkg --print-architecture)
# dpkg-shlibdeps uses installed distribution symbol files to derive ABI bounds.
mkdir -p "$stage/debian"
cp "$project/packaging/debian/control" "$stage/debian/control"
deps=$(cd "$stage" && dpkg-shlibdeps -O -eusr/bin/media-downloader-gtk | sed -n 's/^shlibs:Depends=//p')
[ -n "$deps" ]
rm -rf "$stage/debian"
cat > "$stage/DEBIAN/control" <<CONTROL
Package: media-downloader-gtk
Version: 1.0.0-1
Section: video
Priority: optional
Architecture: $arch
Installed-Size: $(du -sk "$stage/usr" | cut -f1)
Maintainer: Fernando Rojas <ingferchorojas@users.noreply.github.com>
Depends: $deps, yt-dlp (>= 2023.03.04), ffmpeg, python3, ca-certificates
Description: Native GTK4 media downloader powered by yt-dlp
 Spanish desktop interface with asynchronous downloads, a concurrent queue,
 format selection, FFmpeg audio conversion and persistent local preferences.
CONTROL
cp "$project/packaging/debian/copyright" "$stage/usr/share/doc/media-downloader-gtk/copyright"
find "$stage" -type d -exec chmod 755 {} +
find "$stage" -type f -exec chmod 644 {} +
chmod 755 "$stage/usr/bin/media-downloader-gtk"
package="$dist/media-downloader-gtk_1.0.0-1_${arch}.deb"
dpkg-deb --root-owner-group --build "$stage" "$package"
dpkg-deb --info "$package"
printf '\nPackage: %s\n' "$package"
