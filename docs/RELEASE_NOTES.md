# Media Downloader v1.0.0

Native C11 / GTK4 media downloader with a Spanish interface, powered by external
yt-dlp and FFmpeg installations. Includes JSON metadata analysis, compatible
video format selection, audio conversion, structured live progress, a concurrent
queue, cancellation, retries, playlist selection and local preferences.
Checks GitHub for newer engine/application releases at startup, offers a verified
per-user engine update, and preserves spaces in downloaded filenames.
Includes an original YT icon with matching desktop application identity. Overall
progress never decreases within an attempt; 100% is reserved for successful completion.

Release artifacts: architecture-specific Debian package and source code.

Published on 2026-10-11 with explicit owner authorization. The Debian installer
was produced by the successful GitHub CI build. The app does not bypass DRM or
authentication; source-site compatibility depends on yt-dlp.

Known limits: playlists are limited to the first 100 entries per analysis;
metadata unavailable from a site remains unavailable; conversions depend on the
installed FFmpeg decoders and encoders. Unknown video codecs are only accepted
in their reported native container; use audio extraction or a source with full
format metadata for other choices. Download history is session-only.
