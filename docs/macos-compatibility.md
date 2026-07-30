# macOS compatibility plan

macOS support is developed as a CI-first target because the project is not
currently tested on a local macOS machine.

## Current policy

Linux remains the supported platform. macOS is an experimental compatibility
target until the native macOS CI jobs compile and pass the portable test set.

The first compatibility signal has two parts:

- Darwin cross-compilation from Linux for `x86_64-macos` and `aarch64-macos`.
- Native GitHub Actions macOS builds using Homebrew-provided dependencies.

The CI jobs are now blocking compatibility gates. A red macOS job should be
treated as a portability regression or as evidence that the macOS porting layer
needs another targeted fix.

## Scope of the first macOS target

The first macOS target should prove these things in CI:

- the source can be compiled by a Darwin-targeting compiler;
- the source can be compiled by Apple Clang on a GitHub macOS runner;
- GTK 4 can be found through `pkg-config` on macOS;
- the non-GUI tests can run on macOS once the test runner is portable;
- filesystem behavior is tested on a real macOS filesystem.

Interactive GTK validation is deferred until a real Mac is available. CI can
prove compilation, unit behavior, process behavior, and filesystem behavior, but
it cannot fully prove that the application feels correct when used as a desktop
program.

## Cross-compilation role

Cross-compilation is a fast portability check. It is useful for finding wrong
platform guards, Linux-only constants, and unsupported compiler flags in the
portable core.

The Linux-hosted Darwin cross job intentionally does not include Linux GTK or
GLib headers while targeting Darwin. Host GTK headers contain Linux-specific
configuration headers and architecture flags, so mixing them with a Darwin
compiler target produces false failures. The cross job therefore uses a small
cbase syntax-check translation unit. The native GitHub Actions macOS runner is
the source of truth for the full GTK application build because it uses real
macOS headers, libraries, filesystem behavior, process behavior, and Homebrew
packages.

## Native macOS CI role

The native macOS CI job should eventually run:

- `./build.sh build`
- `./build.sh test`
- focused filesystem integration tests
- a conservative GTK startup smoke test

This job is now blocking and is expected to pass as part of the macOS
compatibility gate.

## `fts` policy

macOS provides the `fts(3)` API through `<fts.h>`. The macOS port should keep
using `fts` first, then validate the expected traversal behavior in CI.

The project should still hide traversal behind a small project-owned interface
before adding more non-Linux platforms. That keeps the current Linux/macOS/BSD
implementation simple while preserving an escape hatch for platforms that do not
have usable `fts`.

## Transfer backend policy

Linux keeps using the current rsync backend by default.

For macOS and other non-Linux Unix targets, the project should add an optional
manual recursive copier backend. The manual backend should be tested in CI and
should make its metadata preservation limits visible instead of pretending to be
identical to rsync.


## Build-script portability baseline

The build script now avoids the first layer of Linux/GNU-userland assumptions
that would prevent macOS CI from reaching the C portability work:

- script directory discovery no longer depends on `readlink -f`;
- GTK `pkg-config` compile flags and link flags are collected separately;
- pthread flags are selected by target operating system;
- Linux-only release flags such as `-march=native`, `-flto`, and
  `-ftree-vectorize` are only added for Linux targets;
- developer helpers such as `ctags`, `vtags.sed`, `msgfmt`, `gdb`, and `xsel`
  are optional or replaced with portable fallbacks;
- installation no longer depends on GNU `install -D`;
- test executables use `CECUP_TEST_TMPDIR`, defaulting to `${TMPDIR:-/tmp}`,
  instead of hardcoding `/tmp`.

This does not make the macOS build pass by itself. It only removes build-script
barriers so later steps can address source-level portability failures.

## Platform feature-gate baseline

The first source-level portability layer now uses named feature gates instead of
assuming that every Unix target is Linux/glibc:

- `CBASE_HAS_FTS` controls whether `<fts.h>` is included;
- `CBASE_HAS_PROCFS` keeps `/proc` process scanning Linux-specific;
- `CBASE_HAS_F_GETPATH` detects the macOS `fcntl(F_GETPATH)` feature from the
  actual headers;
- `CBASE_DIRENT_HAS_D_TYPE` gates direct use of `struct dirent.d_type`;
- `CBASE_HAS_SYSTEM_MEMMEM` keeps the GNU `memmem` dependency optional and uses
  the project fallback otherwise;
- `CBASE_HAS_GETTEXT` makes gettext support explicit, with no-op fallbacks when
  the platform/build does not provide libintl.

The build script also defines `_XOPEN_SOURCE=700` for non-Windows targets and
`_DARWIN_C_SOURCE` for Darwin targets so POSIX and Darwin declarations are
available before system headers are parsed.

This step still does not make the complete macOS build pass. The next
expected failures are Linux-specific clock constants and higher-level runtime
assumptions.

## Time API baseline

The code no longer calls Linux clock constants directly outside the cbase time
wrapper.

Use these helpers for elapsed-time measurements:

- `time_monotonic_precise()` for performance timings and total elapsed work;
- `time_monotonic_coarse()` for periodic UI/progress throttling.

On Linux, the wrapper still prefers `CLOCK_MONOTONIC_RAW` for precise timing and
`CLOCK_MONOTONIC_COARSE` for cheap progress checks when the headers expose those
constants. On macOS and BSD, the wrapper falls back to `CLOCK_MONOTONIC` when
the Linux-specific clock IDs are unavailable.

This removes the expected macOS compile failure from direct uses of
`CLOCK_MONOTONIC_COARSE` while keeping the caller intent visible at every call
site.

## Traversal facade baseline

Direct `FTS` usage in application code has been moved behind the `FsWalk`
interface. The first backend is still intentionally thin and keeps the current
`fts(3)` behavior:

- physical traversal, so symlinks are not followed;
- no implicit current-working-directory changes;
- pre-order directory entries for scanning and post-order directory entries for
  recursive removal/rename bookkeeping;
- directory skip support for ignored subtrees;
- path, access path, file name, stat pointer, level, and entry error fields are
  exposed through project-owned names.

For Linux, macOS, and BSD targets with `CBASE_HAS_FTS`, `FsWalk` is currently a
small adapter over `fts_open`, `fts_read`, `fts_set(FTS_SKIP)`, and
`fts_close`. Targets without usable `fts` now have a single place where an
`opendir`/`readdir`/`lstat` fallback can be added later.

## Transfer backend baseline

Transfer execution is now selected through a small backend layer.

The default backend policy is:

- Linux: `rsync`.
- non-Linux Unix targets: `manual`.

The backend can be overridden for testing with `CECUP_TRANSFER_BACKEND=rsync` or
`CECUP_TRANSFER_BACKEND=manual`.

The rsync backend keeps the previous command construction, output parsing,
checksum verification pass, and progress parsing. Deletion handling and task
selection now happen before backend dispatch so they can be shared by all
transfer implementations.

The manual backend is intentionally conservative. It copies from the original
side to the backup side using the task list already computed by preview. It
currently supports:

- directories;
- regular files;
- symlinks;
- hardlinks when multiple source paths with the same file identity are copied in
  the same transfer operation;
- mode and mtime preservation for regular files and directories;
- cancellation between copied items.

The manual backend is not meant to be metadata-equivalent to rsync yet. It does
not preserve owner/group, ACLs, extended attributes, Finder metadata, resource
forks, or special files. That is acceptable for this step because the goal is to
introduce the backend boundary and a usable non-rsync copy path before adding
macOS-specific metadata policy and CI coverage.

## Metadata semantics baseline

Metadata handling is now tied to the selected transfer backend instead of being
left implicit.

The active backend is selected with the same policy used by transfer execution:

- Linux defaults to `rsync`.
- macOS and other non-Linux Unix targets default to `manual`.
- `CECUP_TRANSFER_BACKEND=rsync` and `CECUP_TRANSFER_BACKEND=manual` override
  the default for testing.

The rsync backend keeps the previous preview semantics. The preview compares
owner, group, permissions, directory ctime, file size, file mtime, file type,
symlink targets, and hardlink groups. The transfer command asks rsync to
preserve owner, group, permissions, times, symlinks, and hardlinks.

The manual backend has narrower semantics by design. The preview still compares
file type, regular-file size, mtime, permissions, symlink targets, and hardlink
groups, because the manual backend currently tries to preserve those. The
preview ignores owner, group, and directory ctime differences because the manual
backend does not preserve them. This avoids repeatedly reporting differences
that the selected backend cannot fix.

Both backends currently treat these as unsupported metadata:

- ACLs;
- extended attributes;
- Finder metadata;
- resource forks;
- device nodes, sockets, FIFOs, and other special files.

The transfer log now prints the selected metadata policy before the backend
starts. That makes the selected behavior visible in the UI log and in CI logs.
The future macOS-specific improvement path is to add an optional richer copier,
probably using Apple `copyfile(3)`, and then widen the manual backend policy
only after tests prove the metadata is actually preserved.

## Desktop and configuration portability baseline

Linux-specific desktop/configuration assumptions have been moved behind GLib or
build-time paths.

Runtime configuration now uses GLib's user configuration directory instead of
manually reading `XDG_CONFIG_HOME` and falling back to `$HOME/.config`. The
application creates a `cecup` directory below `g_get_user_config_dir()` and
keeps `cecup.conf` and `ignore.conf` there.

Default configuration seeding no longer shells out to `cp -r` and no longer
hardcodes `/etc/cecup` in the application. At startup, missing user config files
are copied with GLib file APIs from the first available default directory in
this order:

- `CECUP_DEFAULT_CONFIG_DIR`, for tests and local overrides;
- `CECUP_SYSTEM_CONFIG_DIR`, provided by the build script;
- `g_get_system_config_dirs()` plus `cecup`;
- `./etc`, for build-tree development runs.

The build script now defines `CECUP_SYSTEM_CONFIG_DIR` from the selected
installation prefix. Linux keeps `/etc/cecup` by default. Darwin and BSD targets
use `$PREFIX/etc/cecup` by default, matching Homebrew and ports-style layouts.
Install-time config copying now installs individual files with `install` instead
of recursively invoking `cp`.

Locale lookup now uses `CECUP_LOCALEDIR`, `./po` for build-tree runs, and the
compiled `LOCALEDIR` macro. The application no longer hardcodes
`/usr/share/locale` or `/usr/local/share/locale`.

The "open file/folder" menu action now uses GIO's default URI launcher instead
of invoking `xdg-open`. Linux, macOS, and BSD desktop environments can therefore
use their platform/default application handlers through GLib/GIO.

The `.desktop` file is still installed for non-Darwin Unix desktop targets. It
is skipped by default on Darwin; a future packaging step should add a proper
macOS `.app` bundle instead.

## Portable test baseline

The test runner and the filesystem-heavy unit tests now avoid the most obvious
Linux-only test assumptions.

The `test` target exports `CECUP_TEST_TMPDIR`, defaulting to `${TMPDIR:-/tmp}`,
and writes test executables below that directory. C tests that need temporary
filesystem state now use shared helpers for creating temporary directories and
removing directory trees instead of hardcoding `/tmp` paths or invoking
`rm -rf`.

The shared test helpers also probe symlink and hardlink support before running
feature-dependent checks. This matters for macOS CI because filesystem support
can vary by runner volume, permissions, and future sandboxing choices. Tests
that need these features can now skip only that feature-specific assertion path
instead of failing the whole test binary.

The `work_rsync` test now separates rsync-specific coverage from transfer
backend coverage. It still tests rsync output parsing unconditionally, but it
only runs the real rsync command when the installed `rsync` accepts the required
options. The manual recursive copier has dedicated tests for regular files,
directories, symlinks, hardlinks, recursive deletion, root-removal guards, and
the legacy `work_rsync()` thread entry point.

The native macOS CI job now forces `CECUP_TRANSFER_BACKEND=manual` for tests so
macOS portability does not depend on the platform rsync implementation while the
manual backend is being developed.

## CI/build verification baseline

The macOS workflow is now a real compatibility gate instead of a non-blocking
placeholder. It has three jobs:

- build-script syntax checks on Linux, using `sh`, `bash`, and `dash`;
- Darwin syntax checks from Linux with Zig for `x86_64-macos` and
  `aarch64-macos`;
- native macOS build, portable tests, and staged install verification on the
  GitHub-hosted `macos-latest` runner.

The Darwin cross jobs intentionally run in syntax-only mode against a small
portable-core translation unit. They catch target preprocessor and cbase
portability problems without mixing Linux GTK/GLib headers into a Darwin target.
The native macOS job is the source of truth for the full GTK build and runtime
test behavior.

The native macOS job installs GTK 4, gettext, and pkg-config through Homebrew,
then verifies:

- `./build.sh build`;
- the generated `bin/cecup` executable exists and links as a Mach-O binary;
- `./build.sh test` with `CECUP_TRANSFER_BACKEND=manual`;
- `./build.sh install` into a temporary `DESTDIR`;
- Darwin install layout uses `$PREFIX/etc/cecup` for default configuration;
- the Linux `.desktop` file is not installed on Darwin.

The workflow uploads the built executable, generated message catalogs, and
staged install files as diagnostic artifacts when available. Interactive GUI
validation is still outside CI scope and remains a manual release check for a
future macOS package.

## Current CI failure fixes

The Darwin cross syntax source is now generated by `build.sh` in the temporary
build directory instead of depending on a checked-in `src/cross_syntax_check.c`
file. That keeps the job self-contained and avoids stale or missing helper-file
failures while still checking that the compiler target is detected as macOS and
not as Linux/BSD/Windows.

The standalone `update.c` test now initializes and destroys `cecup.arena_mutex`
explicitly. Linux pthread implementations can make a zeroed static mutex appear
to work accidentally, but macOS requires a valid initialized mutex before it is
locked.
