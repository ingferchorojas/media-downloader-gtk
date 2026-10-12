# Media Downloader v1.0.0 — release preparation

Native C11 / GTK4 media downloader with a Spanish interface, powered by external
yt-dlp and FFmpeg installations. Includes JSON metadata analysis, compatible
video format selection, audio conversion, structured live progress, a concurrent
queue, cancellation, retries, playlist selection and local preferences.
Checks GitHub for newer engine/application releases at startup, offers a verified
per-user engine update, and preserves spaces in downloaded filenames.

Release artifacts: architecture-specific Debian package and source code.

This version is prepared locally. No public repository, version tag or release
is created without the project owner's explicit authorization. Broad source-site
compatibility and desktop testing must be reviewed before declaring the release
stable. The app intentionally does not bypass DRM or authentication.

Known limits: playlists are limited to the first 100 entries per analysis;
metadata unavailable from a site remains unavailable; conversions depend on the
installed FFmpeg decoders and encoders. Unknown video codecs are only accepted
in their reported native container; use audio extraction or a source with full
format metadata for other choices. Download history is session-only.
