# Local validation — 2026-10-11

Environment: Ubuntu 24.04.5 LTS, GCC 13.3.0, GTK 4.14.5, GLib 2.80.0,
JSON-GLib 1.8.0, Meson 1.3.2, Ninja 1.11.1, yt-dlp 2025.09.05, FFmpeg 6.1.1.
Development dependencies were extracted inside the ignored `.tools/` directory;
no host packages or global system settings were changed.

## Verified

- C11 compilation in debug, optimized release and ASan/UBSan configurations,
  with project warnings treated as errors.
- 19 GLib unit tests, including metadata, playlists, container compatibility,
  fallback/silent formats, speed units, monotonic progress across revised estimates,
  track changes and processing, queue states, retries,
  subprocess success/failure/cancellation, descendant cleanup after parent exit,
  readable errors and preference persistence.
- Real HTTP downloads through yt-dlp of original FFmpeg-generated media:
  MP4 plus MP3, M4A, Opus, FLAC and WAV conversion, verified using ffprobe.
- Actual DASH fragment downloads and merging of separate H.264/AAC tracks
  to MP4 and MKV, and VP9/Opus tracks to WebM. ffprobe confirms both streams.
- Four queued jobs with a measured maximum of two active jobs; actual active
  download cancellation; HTTP 404 failure propagation; existing output preservation.
- Core integration suite under AddressSanitizer and UndefinedBehaviorSanitizer
  without reported errors, plus optimized-release integration.
- GTK widgets on an isolated Xvfb display with fatal GLib warnings: analysis,
  enabled/disabled controls, downloaded and converted thumbnail, download,
  completion and clean close. The initial screenshot used generated test content.
- Embedded custom YT icon lookup in the GTK theme and a desktop entry whose
  basename matches the GtkApplication identifier.
- Integration callbacks assert nondecreasing progress and reserve 100% for success.
- Debian package creation, contents inspection and dynamic dependency generation.
- APT installation in disposable Ubuntu 24.04 containers, desktop-file validation,
  linkage checks, startup as an unprivileged test user, and APT purge with removal
  of the binary, desktop entry and icon. The same packaging procedure was verified
  before the final optimization and presentation refinements; final release
  artifacts are also staged and inspected locally.

## Owner-requested real YouTube MP3 test

On 2026-10-11, the owner-provided video `1AopjlDWJ8o` was analyzed and downloaded
through the actual GTK interface as MP3 into the requested Downloads folder.
The radio/playlist parameters were omitted so only that video was downloaded.
ffprobe confirmed one MP3 audio stream, no video stream, duration 229.564 seconds
and size 6,639,177 bytes. The README screenshot records the completed job.
The downloaded audio remains outside the repository.

The system yt-dlp 2025.09.05 failed YouTube extraction. The successful test used
a workspace-only official yt-dlp 2026.08.19 executable (SHA-256 checked against
the release checksum list), its bundled EJS components, and already-installed
Node 22.23.3. Node was explicitly enabled for the test through a local launcher.
No system installation was modified, and no cookies or authentication bypasses
were used. Consult the upstream EJS guide for current engine setup requirements.
This verifies the supplied video, not every YouTube video or source website.

## Startup updates and filename refinements

- Added libsoup 3.4.4 for bounded HTTPS requests in cancellable worker tasks.
- Live GTK button test detected the older system engine, downloaded the official
  yt-dlp 2026.08.19 release, verified its digest and executable version, installed
  it inside an isolated XDG data directory, and used it for subsequent analysis
  and completion of the owner-provided video job. The existing MP3 was preserved.
  Compatible Node 22.23.3 was selected by the actual C engine integration from PATH.
  A further live test succeeded with the original Node 20 PATH: the app detected
  the already-installed Node 22.23.3 under NVM without changing the environment.
- GitHub's actual application endpoint returned no published Releases; the UI
  displayed the corresponding status. Newer application version comparison and
  Release-page validation are covered with deterministic JSON fixtures.
- Tests reject corrupt SHA-256, mismatched executable versions and cancellation,
  preserving the prior executable byte-for-byte; successful atomic installation
  selects the managed executable and leaves no staging files.
- Real generated-media integration with the updated engine verified MP4 and all
  audio outputs retain spaces in filenames, alongside the existing merge, queue,
  cancellation, error and no-overwrite checks.
- The owner-requested MP3 in Downloads was renamed with spaces using the engine's
  normal filename sanitization. A slash in the title becomes a safe Unicode slash.
  Updated README screenshot shows the new title and actual update statuses.
- Rebuilt the Debian artifact with libsoup, Python and TLS certificate dependencies;
  the updated package passed APT installation, non-root GUI startup and purge
  inside disposable Ubuntu 24.04.

## Practical limits / not verified

- No claim of compatibility with every supported yt-dlp website. Tests intentionally
  use locally generated authorized content; site-specific extraction depends on
  the installed yt-dlp version and source-site requirements.
- No manual end-to-end review of every desktop preference or file-manager integration.
- Playlist JSON parsing is tested; a real external playlist and bulk-download UX
  still need owner review. The first 100 items per analysis are supported.
- Debian-specific binaries and installation have not been tested on Debian itself.
  Build there to obtain the correct ABI dependency names.
- The public repository was created with owner authorization on 2026-10-11.
  GitHub Actions run on pushes to main; check the Actions tab for remote results.
  No version tag or Release has been created.
- Release metadata uses the planned 1.0.0 version; this is release preparation,
  not a claim that broad stable-release acceptance testing is complete.

Reproduction commands are in README.md. Raw development logs remain ignored in
build directories; they are not published because Meson logs include local
environment details.

## Fragment progress regression — owner-provided video

The video `7mtza4rqxTo` (THE MATRIX: La Película que Cambió al Mundo) reproduced
an initial yt-dlp event with 712 downloaded bytes, a 712-byte estimate, zero
completed fragments and 255 total fragments. The previous calculation incorrectly
latched this temporary 100% track estimate at 47.5% overall. A regression test
now reproduces this event and subsequent changes to the estimate. Fragmented
downloads use completed fragments whenever the byte total is estimated; the
existing monotonic accumulator and completion-only 100% remain in effect.
