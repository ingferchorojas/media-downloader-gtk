# Publication status

Public repository: [ingferchorojas/media-downloader-gtk](https://github.com/ingferchorojas/media-downloader-gtk).
Created and uploaded with explicit owner authorization on 2026-10-11.
Branch: `main`. License: MIT, Fernando Rojas, 2026.
Description: Native C11 and GTK4 media downloader powered by yt-dlp and FFmpeg.

Previous version [v1.0.0](https://github.com/ingferchorojas/media-downloader-gtk/releases/tag/v1.0.0)
was publicly released with explicit owner authorization on 2026-10-11.
The tag points to `89931e85aaf55cccea06c2c0cddb373b0435179c`. Its Ubuntu 24.04
amd64 installer comes from successful CI run `38191659635`. This release remains available as a historical version.

No repository or release is created by the local build or test scripts.
Build outputs, downloaded development packages, local configuration and logs
are ignored. CI receives read-only repository permissions; only the manually
invoked release-draft job receives contents-write permission.

Future version tags and releases require owner authorization. The optional draft
workflow remains available after that authorization:

```sh
git tag -a v1.1.0 -m 'Media Downloader GTK v1.1.0'
git push origin v1.1.0
gh workflow run release.yml -f tag=v1.1.0
```

The workflow produces an unpublished draft with a Debian binary attachment.
Public release publication remains a separate explicit action by the owner.

## v1.1.0

The owner explicitly authorized preparing and publishing v1.1.0 on 2026-10-12.
The app version, Debian changelog, package generation and download documentation
use 1.1.0. Release assets are the Ubuntu 24.04 amd64 installer and `SHA256SUMS`.
The release is published only after its tagged commit passes GitHub CI; the
installer is taken from that exact successful CI run.
