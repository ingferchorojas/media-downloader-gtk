# Publication status

Public repository: [ingferchorojas/media-downloader-gtk](https://github.com/ingferchorojas/media-downloader-gtk).
Created and uploaded with explicit owner authorization on 2026-10-11.
Branch: `main`. License: MIT, Fernando Rojas, 2026.
Description: Native C11 and GTK4 media downloader powered by yt-dlp and FFmpeg.

No repository or release is created by the local build or test scripts.
Build outputs, downloaded development packages, local configuration and logs
are ignored. CI receives read-only repository permissions; only the manually
invoked release-draft job receives contents-write permission.

Review the first CI run and source-site compatibility before approving the first
stable version. Creating/pushing a version tag and creating a release require
separate owner authorization. After that authorization:

```sh
git tag -a v1.0.0 -m 'Media Downloader GTK v1.0.0'
git push origin v1.0.0
gh workflow run release.yml -f tag=v1.0.0
```

The workflow produces an unpublished draft with a Debian binary attachment.
Public release publication remains a separate explicit action by the owner.
