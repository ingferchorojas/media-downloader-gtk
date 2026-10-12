#!/bin/sh
# Run inside a disposable Ubuntu 24.04 container, as root.
set -eu
package=${1:?Usage: verify-package.sh /path/to/package.deb}
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends "$package" xvfb dbus-x11 desktop-file-utils xauth at-spi2-core fonts-dejavu-core
command -v media-downloader-gtk
command -v yt-dlp
command -v ffmpeg
desktop-file-validate /usr/share/applications/media-downloader-gtk.desktop
ldd /usr/bin/media-downloader-gtk > /tmp/md-linkage.txt
if grep -q 'not found' /tmp/md-linkage.txt; then cat /tmp/md-linkage.txt; exit 1; fi
useradd -m md-test
set +e
su -s /bin/sh md-test -c 'timeout 5 dbus-run-session -- xvfb-run -a env G_DEBUG=fatal-warnings GSK_RENDERER=cairo media-downloader-gtk' > /tmp/md-smoke.log 2>&1
status=$?
set -e
cat /tmp/md-smoke.log
[ "$status" -eq 124 ]
apt-get purge -y media-downloader-gtk
[ ! -e /usr/bin/media-downloader-gtk ]
[ ! -e /usr/share/applications/media-downloader-gtk.desktop ]
[ ! -e /usr/share/icons/hicolor/scalable/apps/media-downloader-gtk.svg ]
printf '\nAPT installation, non-root GUI startup and removal verified.\n'
