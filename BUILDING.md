# Building

The plugins are built for 64-bit Windows with CMake and Visual Studio 2022, the same way the official OBS plugin template does it: CMake downloads the OBS Studio sources and the pre-built dependency bundles that match one OBS release, builds `libobs` and `obs-frontend-api` from them, and then builds the plugins against those.

## What you need

- Windows 10 or 11, 64-bit
- Visual Studio 2022 or Build Tools for Visual Studio 2022, with **Desktop development with C++** (this includes CMake) and a Windows SDK of version 10.0.20348 or newer
- An internet connection for the first run (about 350 MB of downloads)

Nothing is installed on the PC. Everything that is downloaded goes into `.deps\` inside this folder.

## Commands

These are the exact commands used for the 1.1.0 release, in PowerShell, from the top folder of the repository. The first two lines point at the CMake that comes with the Build Tools. In a **Developer PowerShell for VS 2022** window `cmake` and `ctest` are already on the path, and you can type them directly.

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"

& $cmake --preset windows-x64
& $cmake --build --preset windows-x64
& $ctest --test-dir build_x64 -C RelWithDebInfo -V
& $cmake --build --preset windows-x64 --target slinglive-package
```

What each one does:

1. **Configure.** The first time, this downloads the three archives listed in `buildspec.json` into `.deps\`, checks each against its SHA-256, and builds `libobs` and `obs-frontend-api`. That takes a few minutes. Later runs skip all of it.
2. **Build.** Produces `build_x64\slinglive\RelWithDebInfo\slinglive.dll`.
3. **Check.** Runs `slinglive-selftest`, described below. It needs no OBS and shows nothing on screen.
4. **Package.** Produces `dist\slinglive-1.1.0-windows-x64.zip`. This is the file to attach to a release. `dist\` is not committed.

Versions used for 1.1.0: Build Tools for Visual Studio 2022 17.13.6 (MSVC 19.43), CMake 3.30.5, Windows SDK 10.0.22621.0, OBS Studio 32.2.2 sources, obs-deps 2026-07-15, Qt 6.11.1.

## Trying a build in OBS

The zip is laid out like the OBS Studio folder, so extracting it into an OBS folder installs the plugin. For a portable copy of OBS:

```powershell
tar -xf dist\slinglive-1.1.0-windows-x64.zip -C "C:\path\to\portable-obs"
```

Then start that OBS and tick **Docks > SlingLive**.

## The self-test

SlingLive has a check that needs no screen. It plays three scripted flings through the game in practice mode and reports which buttons each one pressed. Then it drops the head into the Plinko board from 31 places across the panel, on panels of three heights, and reports whether each drop came down in one slot and whether every slot was reached. Nothing in OBS is started or stopped.

- Without OBS: step 3 above. With a folder as the second argument, `slinglive-selftest` also paints the dock into PNG files there. `ctest` passes `build_x64\slinglive-pictures`.
- Inside OBS: start OBS with the environment variable `SLINGLIVE_SELFTEST=1`. The results are written to the OBS log once OBS has finished loading. Add `SLINGLIVE_SELFTEST_PICTURES=<folder>` for the pictures.

A passing run looks like this:

```
[slinglive] self-test: 640 x 430 panel, practice mode, 120 steps a second
[slinglive] self-test: pull 151 degrees, strength 88 -> Studio Mode, Settings (same at 57 of 57 clock offsets; expected Studio Mode, Settings: OK)
[slinglive] self-test: pull 103 degrees, strength 80 -> Start Recording, Start Streaming, Studio Mode (same at 57 of 57 clock offsets; expected Start Recording, Start Streaming, Studio Mode: OK)
[slinglive] self-test: pull 138 degrees, strength 76 -> Start Streaming, Exit (same at 57 of 57 clock offsets; expected Start Streaming, Exit: OK)
[slinglive] self-test: passed (3 of 3 flings as expected)
[slinglive] self-test: Plinko, 640 x 260 panel, 31 drops -> 31 came down in one slot each (the slowest after 1.8 seconds), 6 of 6 slots reached: OK
[slinglive] self-test: Plinko, 640 x 430 panel, 31 drops -> 31 came down in one slot each (the slowest after 3.7 seconds), 6 of 6 slots reached: OK
[slinglive] self-test: Plinko, 640 x 640 panel, 31 drops -> 31 came down in one slot each (the slowest after 6.1 seconds), 6 of 6 slots reached: OK
[slinglive] self-test: Plinko, practice mode -> 0 calls to OBS (real mode, with a stand-in for OBS: 80 calls for 80 presses): OK
[slinglive] self-test: Plinko passed (3 of 3 panels as expected, practice mode as expected)
```

## Building for another OBS release

A plugin must be built with the same Qt that the OBS release ships, and OBS will not load a plugin built for a newer OBS than itself. To move to another release, change the three entries in `buildspec.json`:

- `obs-studio`: the release tag, and the SHA-256 of its source zip from GitHub.
- `prebuilt` and `qt6`: the obs-deps version and the `windows-x64` hashes. OBS lists them in `CMakePresets.json` at that tag (in older releases, `buildspec.json`).

Then run the commands above again.

## Adding a plugin

Each plugin is a folder beside `slinglive\` with its own `CMakeLists.txt` that calls `sarxina_add_obs_plugin` (see `cmake\ObsPlugin.cmake`). Add the folder to the top `CMakeLists.txt` with `add_subdirectory`, and give it a section in `README.md`.
