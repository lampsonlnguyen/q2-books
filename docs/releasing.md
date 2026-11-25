# Build, test and publish Books

## Local host build (Ubuntu/WSL)

```sh
sudo apt-get update
sudo apt-get install -y git cmake g++ clang libexpat1-dev zlib1g-dev libjpeg-dev libcurl4-openssl-dev openssl libfreetype-dev fonts-liberation python3
cmake -S crosspoint-core -B build/core-host -DCMAKE_BUILD_TYPE=Release
cmake --build build/core-host --parallel 2
ctest --test-dir build/core-host --output-on-failure
````

Host builds are not Q2-installable. For the actual MIPS card package, use GitHub Actions or the same `crosspoint-core/tools/build_q2_github.sh` with its required Q2 toolchain/runtime dependencies. See `.github/workflows/release.yml` for the authoritative dependency list and gates.

## Release workflow

Source pushes to main trigger Automatic Books release, which reserves the next patch (or a newer explicit VERSION), tags main and dispatches the validated release workflow. No per-version documentation files are required. Release descriptions are generated during publication; historical tags with old notes can still be retried. Manual prereleases use a version-compatible alpha/beta/rc suffix. To retry an existing tag, run **Build, test and publish Books for Q2** from main with that tag. Never move published tags or overwrite existing populated releases.

Publication requires matching source VERSION, tag ancestry on main, GCC/Clang CTest, Q2 MIPS/QEMU regressions, validated ELF and installable ZIP plus checksum. A reserved tag is not proof that publication succeeded. README/docs-only changes do not reserve versions. Physical hardware QA is separate.

## Install and troubleshoot

Extract the released ZIP to the SD root, not a subfolder. Preserve existing .books data and fonts. Confirm .books/q2app, .books/q2-books and .books/books-engine exist; put books in Books/. Start from Library → Books. If launch fails inspect .books/startup.log and startup.previous.log. Never flash the app as firmware. For optional session profiling create .books/profile-session, launch and exit normally, then inspect the log. Remove the marker to disable profiling. See [technical reference](TECHNICAL.md) for resource constraints.
