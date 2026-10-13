# Local validation — 2026-10-11

Environment: Ubuntu 24.04.5 LTS, GCC 13.3.0, GTK 4.14.5, GLib 2.80.0,
JSON-GLib 1.8.0, Meson 1.3.2, Ninja 1.11.1, yt-dlp 2025.09.05, FFmpeg 6.1.1.
Development dependencies were extracted inside the ignored `.tools/` directory;
no host packages or global system settings were changed.

## Verified

- C11 compilation in debug, optimized release and ASan/UBSan configurations,
  with project warnings treated as errors.
- 21 GLib unit tests, including metadata, playlists, container compatibility,
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
The corrected production download manager downloaded the complete video as MP4
with formats `270+140-0`. All 299 recorded progress notifications were
nondecreasing, beginning at 0% and reaching 100% only on successful completion.
ffprobe confirmed both video and audio streams. Test output is kept in the
ignored `.tools/progress-video-test/verified/` directory.

## Original audio language regression

Owner-provided video `wHTkk8an2I0` (TOP GUN MAVERICK: Supera a la Original?)
exposes English (US) tracks with `language_preference=-1` and original/default
Spanish (US) tracks with `language_preference=10`. Its English Opus track has a
higher bitrate than its Spanish Opus track, reproducing the previous selector's
wrong-language choice for MKV. The parser now preserves language and extractor
preference; the selector compares language preference before audio quality.
A regression test covers MKV, MP4, MP3 and FLAC, including fallback quality
ranking when preferences are equal. The format hint displays the audio language.
The corrected production manager downloaded this video as MKV with selector
`616+140-1`. One initial audio request returned HTTP 403; retrying completed
successfully, reusing the already downloaded video track. ffprobe confirmed both
streams. SHA-256 hashes of the copied audio packet payloads in the final MKV and
a separate download of the original Spanish `140-1` track match exactly.
The test files remain in ignored `.tools/audio-language-test/`.

## Light/dark appearance regression

The host desktop uses `Yaru-dark`; merely setting GTK’s dark preference cannot
switch a dark-only theme to light. Explicit choices now load GTK’s built-in
Adwaita variant above desktop styling and below application CSS. System mode
removes the override. The button checks the rendered theme background to choose
the opposite appearance and preserves the choice in preferences.
GTK regression tests click the actual button twice and assert visible background
colors and saved preferences for both states. The CI GUI suite forces
`GTK_THEME=Adwaita:dark` to reproduce the original problem.
Local button tests passed with both `GTK_THEME=Adwaita:dark` and
`GTK_THEME=Yaru-dark`. A separate restart with a forced light desktop theme
verified that the previously saved dark appearance takes effect at startup.
The isolated-config tests did not change the user's desktop or app preferences.

## Queue cleanup and active download ordering

A new manager display-order test verifies that individual active downloads move
ahead of old/queued rows, concurrent downloads retain stable relative order,
playlist rows retain queue order while pending, and scheduling order is unchanged.
The GTK test downloads local generated media, starts a second job and verifies
that its row appears first. It clicks **Limpiar descargas** during analysis,
checks that only the finished row is removed and the active row survives, then
clears the final completed row and checks the empty list and disabled button.
The harness confirms the saved media still exists after clearing the queue.

## Combined download size — 2026-10-12

Debug compilation and all 22 core tests pass. New regression checks cover unequal
video/audio sizes, cumulative bytes across track transitions, initialization
fragment estimates, unavailable audio sizes, and the 99% processing cap.
The local HTTP integration suite passes for direct downloads, six output formats,
DASH video/audio merging into MP4, MKV and WebM, queue concurrency, cancellation,
HTTP errors and preservation of existing files. Integration callbacks additionally
check that progress events match the selected tracks, display a combined total,
and do not report downloaded bytes above a known total.

The combined total now stays fixed until all tracks finish. The weighted-progress
regression also checks changing fragment estimates, downloaded bytes exceeding
an initial estimate, track completion with a different size, retention of `≈`
during the download, and the final actual combined size. Optimized compilation
and all 22 core tests pass for this change.

## Retry selection and playlist numbering — 2026-10-12

Optimized compilation and all 23 core tests pass. Coverage includes saved retry
counts and bounds (1–3), saved numbering preference, original playlist positions
including unavailable entries, filename prefixes and preservation on manual retry.
The local HTTP integration suite and GTK UI suite under Xvfb pass. A numbered
job produces a playable `007 - … .mp4` with one configured retry. The UI test
selects each retry count and checks the saved preference. The updated Debian
package was generated from the optimized build.

## Strict output-format verification — 2026-10-12

Release and ASan/UBSan core suites pass all 24 tests. A successful yt-dlp exit
now triggers asynchronous FFprobe verification before a private staged output
is published to the destination. Validation checks extension, container, audio
codec and absence of video in audio exports; attached cover art is accepted.
Each playlist batch captures one selected format before enqueueing its jobs.

The local HTTP integration and GTK UI suites pass, including all six native/
audio outputs, DASH merging, cancellation, queue concurrency and preservation
of existing video output. Added format regressions also pass under ASan/UBSan:
four numbered MP3 jobs with mixed MP4 and DASH sources, preservation of a valid
existing MP3, rejection of an MP4 disguised as an existing MP3, and rejection of
an engine that exits successfully but returns a video for an MP3 request.
Incorrect outputs remain in the private staging folder and are never reported
as completed or published as playlist results. Conversion overwrites are allowed
only within the private staging folder; existing destination files stay protected.

Five owner-reported MP4 files in the music directory were converted locally with
FFmpeg and verified as MP3 audio. Their original videos were preserved in the
hidden `.originales-convertidos` folder. The release Debian package was rebuilt.

## Processing retries — 2026-10-12

Failed processing and failed final-format validation now reuse the same staged
job and argument snapshot for the selected 1–3 additional attempts. Cancellation
and manager shutdown stop retries; manual retry resets the processing counter.
`tests/processing-retries.py` passes in release and ASan/UBSan builds. Its controlled
engine simulates conversion failures and invokes real FFmpeg on recovery. For
each configured retry count, it checks recovery, exhaustion after exactly N+1
total attempts, one source download, no visible video output, and cancellation
before another processing attempt starts. The 24 core tests still pass, and the
release Debian package was rebuilt.

## Retry range and delay — 2026-10-12

The retry selector accepts 1–10 and retains existing settings. Retries for
processing and transient metadata/connection failures wait 3, 6, 12, 24, then
at most 30 seconds. Temporary connection failures also refresh metadata before
resuming the same staged job; known unavailable/private content does not enter
connection retries. Cancelling removes a pending timer immediately.

All 25 core tests pass. Release and ASan/UBSan retry regressions verify delayed
processing recovery, exact exhaustion counts, reuse of downloaded source files,
recovery after two HTTP 503 analysis failures, bounded analysis exhaustion,
no retry for HTTP 404, and cancellation of both pending retry types. The Xvfb
UI test selects and persists all ten retry values. The release package was rebuilt.

## Close confirmation and cancel-all — 2026-10-12

Release and ASan/UBSan core suites pass all 26 tests. Batch cancellation prevents
queued work from starting during cancellation, preserves completed jobs and
restores the ability to enqueue new downloads. The Xvfb UI regression with
`MD_UI_CANCEL_CONTROLS=1` passes with fatal GLib warnings: no batch button for
zero/one unfinished job, button present for multiple unfinished jobs, declining
close/cancel dialogs preserves work, accepted batch cancellation cancels active
and waiting jobs, and a new download can be queued afterward. Accepting close
with that new job pending shuts down cleanly after callbacks finish. The release
Debian package was rebuilt.

## v1.1.0 release preparation — 2026-10-12

The application reports version 1.1.0 and the locally generated Debian package
reports 1.1.0-1, amd64. All 26 release core tests pass. The current interface was
captured from a successful local-media GUI run under an isolated X11/Xvfb display.
Packaging now reads the version from the Debian changelog. The GitHub build also
runs the processing/connection retry and close/batch-cancel GUI regressions before
verifying package installation. The public installer is taken from that release
commit's successful GitHub CI artifact, with a matching checksum attachment.
