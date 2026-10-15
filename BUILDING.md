# Building

The plugins are built for 64-bit Windows with CMake and Visual Studio 2022, the same way the official OBS plugin template does it: CMake downloads the OBS Studio sources and the pre-built dependency bundles that match one OBS release, builds `libobs` and `obs-frontend-api` from them, and then builds the plugins against those.

## How the code is laid out

Each plugin is a dock that stands in for OBS's Controls panel. Everything they have in common is in `shared\`, and each plugin's folder holds only what makes it different: its game.

- `shared\src\control-panel` is the panel: the buttons, what a press does in practice and real mode, knocked-over buttons, sparks and the power-off gag. `controls.hpp` lists the buttons, `physics.hpp` has the falling and bouncing.
- `shared\src\game.hpp` is what a plugin brings: where the buttons stand, how the head moves and what it hits, what it draws, and how short its panel can be. A dock wider than that shows the whole panel in its middle.
- `shared\src\controls-dock`, `panel-painter` and `obs-bridge` are the dock in OBS, how it looks, and the link to OBS.
- `slinglive\src\slingshot` and `plinkolive\src\plinko-board` are the two games.

## What you need

- Windows 10 or 11, 64-bit
- Visual Studio 2022 or Build Tools for Visual Studio 2022, with **Desktop development with C++** (this includes CMake) and a Windows SDK of version 10.0.20348 or newer
- An internet connection for the first run (about 350 MB of downloads)

Nothing is installed on the PC. Everything that is downloaded goes into `.deps\` inside this folder.

## Commands

In PowerShell, from the top folder of the repository. The first two lines point at the CMake that comes with the Build Tools. In a **Developer PowerShell for VS 2022** window `cmake` and `ctest` are already on the path, and you can type them directly.

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"

& $cmake --preset windows-x64
& $cmake --build --preset windows-x64
& $ctest --test-dir build_x64 -C RelWithDebInfo -V
& $cmake --build --preset windows-x64 --target slinglive-package plinkolive-package
```

What each one does:

1. **Configure.** The first time, this downloads the three archives listed in `buildspec.json` into `.deps\`, checks each against its SHA-256, and builds `libobs` and `obs-frontend-api`. That takes a few minutes. Later runs skip all of it.
2. **Build.** Produces `build_x64\slinglive\RelWithDebInfo\slinglive.dll` and `build_x64\plinkolive\RelWithDebInfo\plinkolive.dll`.
3. **Check.** Runs each plugin's self-test, described below. It needs no OBS and shows nothing on screen.
4. **Package.** Produces `dist\slinglive-<version>-windows-x64.zip` and `dist\plinkolive-<version>-windows-x64.zip`, the files to attach to a release. `dist\` is not committed.

Versions used: Build Tools for Visual Studio 2022 17.13.6 (MSVC 19.43), CMake 3.30.5, Windows SDK 10.0.22621.0, OBS Studio 32.2.2 sources, obs-deps 2026-07-15, Qt 6.11.1.

## Trying a build in OBS

Each zip is laid out like the OBS Studio folder, so extracting it into an OBS folder installs the plugin. For a portable copy of OBS:

```powershell
tar -xf dist\plinkolive-1.1.0-windows-x64.zip -C "C:\path\to\portable-obs"
```

Then start that OBS and tick the plugin under **Docks**.

## The self-tests

Each plugin has a check that needs no screen. Nothing in OBS is started or stopped.

- **SlingLive** plays three scripted flings in practice mode, each with the bobbing buttons at 57 different points, and reports which buttons each one pressed.
- **PlinkoLive** drops the head from the top 2000 times, each with the random nudges at the pegs seeded differently, and reports what share of the drops came down in each slot (every slot has to get at least 1 in 100), that the same drops come down in the same slots on two taller panels, and that practice mode never reached OBS.

Without OBS, step 3 above runs them. With a folder as the second argument, `<plugin>-selftest` also paints the dock into PNG files there; `ctest` passes `build_x64\<plugin>-pictures`. Inside OBS, start OBS with the environment variable `SLINGLIVE_SELFTEST=1` or `PLINKOLIVE_SELFTEST=1`; the results are written to the OBS log once OBS has finished loading. Add `SLINGLIVE_SELFTEST_PICTURES=<folder>` (or the PlinkoLive one) for the pictures.

A passing run looks like this:

```
[SlingLive] self-test: 640 x 430 panel, practice mode, 120 steps a second
[SlingLive] self-test: pull 151 degrees, strength 88 -> Studio Mode, Settings (same at 57 of 57 clock offsets; expected Studio Mode, Settings: OK)
[SlingLive] self-test: pull 103 degrees, strength 80 -> Start Recording, Start Streaming, Studio Mode (same at 57 of 57 clock offsets; expected Start Recording, Start Streaming, Studio Mode: OK)
[SlingLive] self-test: pull 138 degrees, strength 76 -> Start Streaming, Exit (same at 57 of 57 clock offsets; expected Start Streaming, Exit: OK)
[SlingLive] self-test: passed (3 of 3 flings as expected)
[PlinkoLive] self-test: 640 x 430 panel, 2000 drops from the top -> 2000 came down in one slot each (the slowest after 5.4 seconds): Start Streaming 2.2%, Start Recording 13.2%, Start Virtual Camera 36.0%, Studio Mode 34.5%, Settings 11.2%, Exit 2.8%: OK
[PlinkoLive] self-test: 640 x 640 panel, the same 2000 drops -> 2000 came down in the same slot as on the 640 x 430 panel (the slowest after 5.8 seconds): OK
[PlinkoLive] self-test: 640 x 1024 panel, the same 2000 drops -> 2000 came down in the same slot as on the 640 x 430 panel (the slowest after 6.2 seconds): OK
[PlinkoLive] self-test: practice mode -> 0 calls to OBS (real mode, with a stand-in for OBS: 1944 calls for 1944 presses): OK
[PlinkoLive] self-test: passed (3 of 3 panels as expected, practice mode as expected)
```

## Building for another OBS release

A plugin must be built with the same Qt that the OBS release ships, and OBS will not load a plugin built for a newer OBS than itself. To move to another release, change the three entries in `buildspec.json`:

- `obs-studio`: the release tag, and the SHA-256 of its source zip from GitHub.
- `prebuilt` and `qt6`: the obs-deps version and the `windows-x64` hashes. OBS lists them in `CMakePresets.json` at that tag (in older releases, `buildspec.json`).

Then run the commands above again.

## Adding a plugin

Make a folder beside `slinglive\` with:

- `src\`: a class that implements `sarxina::Game` (see `shared\src\game.hpp`), `makeGame()` returning it, and `runSelfTest()` (see `shared\src\self-test.hpp`).
- `data\locale\en-US.ini` with at least `DockTitle` and `Description`; the shared strings are added to it when it is built.
- `CMakeLists.txt` calling `sarxina_add_obs_plugin` (see `cmake\ObsPlugin.cmake`).

Add the folder to the top `CMakeLists.txt` with `add_subdirectory`, and give the plugin a section in `README.md`.
