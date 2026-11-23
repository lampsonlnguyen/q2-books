# Releasing Books for Shanling Q2

The versioned, installable SD-card release is produced by
[`.github/workflows/release.yml`](../.github/workflows/release.yml).
The `q2-reader-card.yml` workflow is **CI only** and does not publish releases.

## Automatic release

1. Merge the desired changes to `main`.
2. Update `crosspoint-core/VERSION` (for example, `1.2.2`).
3. Add `docs/release-notes/v<version>.md`.
4. Confirm the normal main-branch CI is passing.
5. Push an **unmoved** annotated or lightweight tag from the desired main commit:

   ```sh
   git checkout main
   git pull --ff-only
   git tag v1.2.2
   git push origin v1.2.2
   ```

Pushing a `v*` tag automatically runs the versioned-release workflow.
For a pre-release, use `v1.2.2-beta`, `v1.2.2-beta.1`,
`v1.2.2-alpha.1`, or `v1.2.2-rc.1`, and add a matching
release-notes document.

The pipeline **refuses publication** if the tag is malformed, the source is
not on main, the source version disagrees with the tag, release notes are
missing, the host tests fail, the Q2 build/QEMU regressions fail, or the
produced package does not meet the ZIP, MIPS ABI, and checksum checks.

A successful run publishes `books-q2-vX.Y.Z.zip` and its
`books-q2-vX.Y.Z.zip.sha256` under GitHub Releases. The archive installs
the `.books` card app on the microSD card; **it is not Q2 firmware**.

## Manual retry or repair of an empty GitHub release

Choose **Actions > Build, test and publish Books for Q2 > Run workflow**,
select **main**, and provide the **existing** version tag as `release_tag`.
The run rebuilds and re-tests that tag's source; it does not publish the current
main branch in place of the tag. A pre-existing release may be filled **only**
if it has no assets. A populated release is never overwritten.

Avoid moving or deleting a version tag after publication. Publish a new patch
version instead.

## Historical v1.2.1 note

The `v1.2.1` tag points to code with
`crosspoint-core/VERSION = 1.2.0`, lacks
`docs/release-notes/v1.2.1.md`, and has an empty GitHub Release.
The release gate intentionally refuses to relabel that source as v1.2.1.
The current source on main is version 1.2.2 with release notes prepared;
create its tag only after verifying the desired commit.

## Testing scope

Host GCC/Clang CTest, actual Q2 MIPS ABI cross-compilation, QEMU integration
and rendering tests, EPUB fixtures, font and language tests, connected-library
integration, card launcher and ELF/package validation run before publication.
CI is not a substitute for physical Q2 QA (screen output, click wheel,
suspend/resume, memory, battery, SD card and real network behavior).
