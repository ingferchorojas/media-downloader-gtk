# Publication status

Public repository: [ingferchorojas/media-downloader-gtk](https://github.com/ingferchorojas/media-downloader-gtk).
Created and uploaded with explicit owner authorization on 2026-10-11.
Branch: `main`. License: MIT, Fernando Rojas, 2026.
Description: Native C11 and GTK4 media downloader powered by yt-dlp and FFmpeg.

Version [v1.0.0](https://github.com/ingferchorojas/media-downloader-gtk/releases/tag/v1.0.0)
was publicly released with explicit owner authorization on 2026-10-11.
The tag points to `89931e85aaf55cccea06c2c0cddb373b0435179c`. Its Ubuntu 24.04
amd64 installer comes from successful CI run `38191659635`. GitHub’s latest-release
endpoint returns this published version and its Debian attachment.

No repository or release is created by the local build or test scripts.
Build outputs, downloaded development packages, local configuration and logs
are ignored. CI receives read-only repository permissions; only the manually
invoked release-draft job receives contents-write permission.

Future version tags and releases require owner authorization. The optional draft
workflow remains available after that authorization:

```sh
git tag -a v1.0.0 -m 'Media Downloader GTK v1.0.0'
git push origin v1.0.0
gh workflow run release.yml -f tag=v1.0.0
```

The workflow produces an unpublished draft with a Debian binary attachment.
Public release publication remains a separate explicit action by the owner.
