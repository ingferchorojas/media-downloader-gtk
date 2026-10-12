# Third-party software

The MIT license in LICENSE covers original project code and assets only.
Dependencies retain their own licenses and are not bundled in the Debian package.

- [yt-dlp](https://github.com/yt-dlp/yt-dlp#license): source is Unlicense;
  some release bundles incorporate differently licensed dependencies.
- [FFmpeg](https://ffmpeg.org/legal.html): LGPL-2.1-or-later by default;
  enabling optional GPL components changes the effective build license.
  Ubuntu's build may be GPL. Consult the installed package's copyright file.
- [GTK4](https://docs.gtk.org/gtk4/): LGPL-2.1-or-later.
- [GLib / GIO](https://gitlab.gnome.org/GNOME/glib/-/blob/main/COPYING): LGPL-2.1-or-later.
- [JSON-GLib](https://gitlab.gnome.org/GNOME/json-glib/-/blob/main/COPYING): LGPL-2.1-or-later.

GTK, GLib and JSON-GLib are dynamically linked and remain replaceable system
libraries. yt-dlp and FFmpeg run as external system programs. Redistributors
must respect the license and source availability requirements of any dependency
binaries they separately distribute. Debian copyright information is normally
available in /usr/share/doc/<package>/copyright.
