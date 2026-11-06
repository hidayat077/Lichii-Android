<!--
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->

<p align="center">
  <img src="dist/branding/lichii_icon_with_logo.png" alt="Lichii" width="600">
</p>

# Lichii for Android (Snapdragon 870 / Adreno 650 test fork)

A personal fork of [pavelgajdo/Lichii-Android](https://github.com/pavelgajdo/Lichii-Android), maintained by [hidayat077](https://github.com/hidayat077). The original project is an Android port of the [Lichii](https://github.com/zortos293/Eglantine) Nintendo Switch / Switch 2 emulator. It focuses on getting Switch 2 games running on Android phones, starting with Samsung's Exynos phones and their AMD-based Xclipse GPUs.

This fork is used to experiment with small improvements and to check how the project behaves on a Qualcomm Snapdragon 870 phone (Adreno 650 GPU).

This is an independent, experimental fork. It is not affiliated with Nintendo, with the original Lichii-Android maintainer, or with the upstream projects it builds on.

## Status of this fork

- **No functional changes yet.** The code is currently identical to upstream, apart from documentation and CI-related housekeeping.
- **Not tested on Snapdragon 870 yet.** Nothing in this README claims that Switch 2 games run on this hardware. Treat every Snapdragon-related note below as an expectation or a starting point, not a result.
- Changes are developed on separate branches and checked by the GitHub Actions build before being merged into this fork's `nx2` branch.

## Download

- **This fork's releases:** [github.com/hidayat077/Lichii-Android/releases](https://github.com/hidayat077/Lichii-Android/releases), if a release has been published by CI.
- **Build artifacts:** every CI run uploads the APK as an artifact on its run page under the **Actions** tab. GitHub provides artifacts as a `.zip` file, so extract it to get the `.apk`.
- **Upstream releases:** [github.com/pavelgajdo/Lichii/releases](https://github.com/pavelgajdo/Lichii/releases), the original author's builds.

You may need to allow installs from your browser or file manager before installing an APK. Only install APKs from sources you trust.

## Requirements

An arm64 phone with Android 13 or newer and Vulkan 1.3. Games, keys and firmware are not included. Only use games and system files you have legally obtained.

### Snapdragon 870 (Adreno 650) notes

- The Snapdragon 870 is a 64-bit chip, so the arm64 requirement is met.
- Adreno 650's official specification lists Vulkan 1.1. Whether a given phone's stock driver reports Vulkan 1.3 depends on the manufacturer's driver updates. Check your own device before expecting this app to work.
- The open-source Mesa Turnip driver supports Vulkan 1.3 on Adreno 600-series GPUs, so a custom Turnip driver may close the gap. Whether and how it can be installed through this app's GPU driver manager has **not** been verified in this fork.
- Android version and device-specific behavior (for example Realme UI background restrictions) can also affect results.

## Game status

No games have been tested on Snapdragon 870 in this fork yet.

## Reporting problems

For problems in this fork's own changes, open an issue at [github.com/hidayat077/Lichii-Android/issues](https://github.com/hidayat077/Lichii-Android/issues) (issues must be enabled in the repository settings). For problems in the emulator itself, upstream's tracker is [github.com/pavelgajdo/Lichii/issues](https://github.com/pavelgajdo/Lichii/issues).

Include:

- your phone model, Android version and GPU driver;
- the game and what happened (screenshots help);
- the log from the affected session: `Android/data/dev.lichii.android/files/log/eden_log.txt` (`dev.lichii.android.relWithDebInfo` for test builds).

Check logs for personal information before uploading. Never upload games, keys, firmware or account data.

### Diagnostic switches

Android can't pass environment variables to an app, so Lichii reads them from `Android/data/<app id>/files/env.txt`, one `KEY=VALUE` per line (`#` starts a comment). They take effect the next time the app starts. The available switches are listed in [AGENTS.md](AGENTS.md#debugging-tools). Leave the file empty for normal play: most switches slow the emulator down.

## Building

This fork is normally built by GitHub Actions rather than on a local machine. The workflow is [.github/workflows/android.yml](.github/workflows/android.yml) and runs on every push. A first build on a fresh fork took about 22 minutes in testing.

To build locally instead:

```bash
cd src/android
./gradlew assembleMainlineRelWithDebInfo
```

You need the Android SDK, NDK 28.2 and CMake 3.22. The APK is written to `src/android/app/build/outputs/apk/mainline/relWithDebInfo/`. Pushing a tag named `android-<version>` publishes a release with the APK.

More documentation, inherited from upstream: [build guide](docs/Build.md), [coding guidelines](docs/Coding.md).

### Setting up CI on your own fork

1. GitHub disables workflows on new forks. Open the **Actions** tab and enable them.
2. Under **Settings → Actions → General → Workflow permissions**, choose **Read and write permissions** so the release step can publish.
3. Documentation-only commits can skip the build by adding `[skip ci]` to the commit message.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [docs/Coding.md](docs/Coding.md) before changing code. Keep each change small and focused, and do one improvement per branch. A green CI build shows that the code compiles. It does not prove that the change works correctly on a device, so describe honestly what was and was not tested.

## Credits and license

Lichii for Android builds on [Lichii](https://github.com/zortos293/Eglantine) by Zortos and on Eglantine, Eden, yuzu and Sudachi, and their contributors, including Eglantine's original developer Camille LaVey. Lichii artwork by YOB (yobslab). Original copyright notices and third-party licenses are retained.

This fork is based on [pavelgajdo/Lichii-Android](https://github.com/pavelgajdo/Lichii-Android).

See [LICENSE.txt](LICENSE.txt) and the source files for the applicable license terms.
