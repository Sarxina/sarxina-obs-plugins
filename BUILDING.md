# Building

The plugins are built for 64-bit Windows with CMake and Visual Studio 2022, the same way the official OBS plugin template does it: CMake downloads the OBS Studio sources and the pre-built dependency bundles that match one OBS release, builds `libobs` and `obs-frontend-api` from them, and then builds the plugins against those.

## How the code is laid out

SlingLive and PlinkoLive are docks that stand in for OBS's Controls panel. Everything they have in common is in `shared\`, and each plugin's folder holds only what makes it different: its game.

- `shared\src\control-panel` is the panel: the buttons, what a press does in practice and real mode, knocked-over buttons, sparks and the power-off gag. `controls.hpp` lists the buttons, `physics.hpp` has the falling and bouncing.
- `shared\src\game.hpp` is what a plugin brings: where the buttons stand, how the head moves and what it hits, what it draws, and how short its panel can be. A dock wider than that shows the whole panel in its middle.
- `shared\src\controls-dock`, `panel-painter` and `obs-bridge` are the dock in OBS, how it looks, and the link to OBS.
- `slinglive\src\slingshot` and `plinkolive\src\plinko-board` are the two games.

Sarxina Parametric EQ, in `sarxina-parametric-eq\`, is an audio filter rather than a dock and uses none of the shared dock code: `eq-design` is the equalizer (Audition's band layout as biquad sections, and the processor that runs them), `eq-settings` its settings as OBS keeps them, `curve-editor` the window with the curve, and `plugin-main` the filter in OBS.

The Sarxina OBS Plugin Manager is a plugin of its own, in `sarxina-obs-plugin-manager\`:

- `shared\src\sarxina-obs-plugin-manager.h` is all that the manager and a plugin share. Plugins build against it and reach the manager through OBS's global procedure handler, so neither links the other, and a plugin runs on its own when the manager is not installed.
- `manager` keeps the plugins that registered and whether each is added; `manager-dock` is the dock, and `properties-form` draws the settings behind a plugin's gear from what the plugin says about them.
- `chat-router` is the one way chat reaches plugins; the checks every message passes are listed at the top of `chat-router.hpp`. `twitch-chat` reads, without logging in, the chat of the Twitch account OBS is connected to (Settings > Stream > Connect Account; the manager reads only the account's name from the OBS profile, never its tokens). `vtube-studio` is the one VTube Studio connection plugins share, and `websocket` carries both.
- `examples\example-plugin` is the smallest plugin the manager can run (see the last section).

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
& $cmake --build --preset windows-x64 --target slinglive-package plinkolive-package sarxina-obs-plugin-manager-package sarxina-parametric-eq-package
```

What each one does:

1. **Configure.** The first time, this downloads the three archives listed in `buildspec.json` into `.deps\`, checks each against its SHA-256, and builds `libobs` and `obs-frontend-api`. That takes a few minutes. Later runs skip all of it.
2. **Build.** Produces `<plugin>.dll` in `build_x64\<plugin>\RelWithDebInfo\` for `slinglive`, `plinkolive`, `sarxina-obs-plugin-manager` and `sarxina-parametric-eq`, and the example plugin in `build_x64\examples\example-plugin\RelWithDebInfo\`.
3. **Check.** Runs each plugin's self-test, described below. It needs no OBS and no internet, and shows nothing on screen.
4. **Package.** Produces `dist\<plugin>-<version>-windows-x64.zip` for each of the four, the files to attach to a release. `dist\` is not committed. The example plugin is never packaged.

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
- **PlinkoLive** drops the head from the top 2000 times, each with the random nudges at the pegs seeded differently, and reports what share of the drops pressed each button (every button has to get at least 1 in 100), that the same drops press the same buttons on two taller panels, and that practice mode never reached OBS.
- **Sarxina OBS Plugin Manager** reads lines of Twitch's chat protocol and the Twitch account from OBS profiles, puts messages that should be dropped through the checks, and routes commands from different chatters and roles at different times to a plugin that exists only in the test. Then it goes the whole way over real websockets to stand-ins for Twitch and VTube Studio on the same computer: logging in to chat anonymously, a command reaching the plugin, reconnecting; and with VTube Studio, asking for permission once, logging in with the remembered token, never asking again on its own, and a plugin's request with its answer.

- **Sarxina Parametric EQ** checks each kind of band against the textbook (a band's gain at its centre and half of it at its edges, a shelf's half gain at its corner, a pass filter's -3 dB at its cutoff and its fall an octave out, for every slope), runs an impulse through the filter as OBS would and measures it against the drawn curve at 400 frequencies, and takes the settings through OBS's settings data and back. With a folder it also paints the editor and writes `response.csv`.

Without OBS, step 3 above runs them. With a folder as the second argument, `<plugin>-selftest` also paints the dock into PNG files there; `ctest` passes `build_x64\<plugin>-pictures`. Inside OBS, start OBS with the environment variable `SLINGLIVE_SELFTEST=1`, `PLINKOLIVE_SELFTEST=1`, `SARXINA_OBS_PLUGIN_MANAGER_SELFTEST=1` or `SARXINA_PARAMETRIC_EQ_SELFTEST=1`; the results are written to the OBS log once OBS has finished loading. Add `SLINGLIVE_SELFTEST_PICTURES=<folder>` (and likewise for the others) for the pictures. Inside OBS the manager's self-test also lists the plugins that registered through OBS and, once VTube Studio is connected, asks it which model is loaded, the way a plugin would. With `SARXINA_OBS_PLUGIN_MANAGER_SELFTEST_PROFILE=<a profile's basic.ini>` it also says which Twitch account that profile is connected to, by name only.

A passing run looks like this:

```
[SlingLive] self-test: 640 x 430 panel, practice mode, 120 steps a second
[SlingLive] self-test: pull 151 degrees, strength 88 -> Studio Mode, Settings (same at 57 of 57 clock offsets; expected Studio Mode, Settings: OK)
[SlingLive] self-test: pull 103 degrees, strength 80 -> Start Recording, Start Streaming, Studio Mode (same at 57 of 57 clock offsets; expected Start Recording, Start Streaming, Studio Mode: OK)
[SlingLive] self-test: pull 138 degrees, strength 76 -> Start Streaming, Exit (same at 57 of 57 clock offsets; expected Start Streaming, Exit: OK)
[SlingLive] self-test: passed (3 of 3 flings as expected)
[PlinkoLive] self-test: 640 x 430 panel, 2000 drops from the top -> 2000 pressed one button each (the slowest after 5.4 seconds), left to right: Start Virtual Camera 2.2%, Studio Mode 13.2%, Start Streaming 36.0%, Start Recording 34.5%, Settings 11.2%, Exit 2.8%: OK
[PlinkoLive] self-test: 640 x 640 panel, the same 2000 drops -> 2000 pressed the same button as on the 640 x 430 panel (the slowest after 5.8 seconds): OK
[PlinkoLive] self-test: 640 x 1024 panel, the same 2000 drops -> 2000 pressed the same button as on the 640 x 430 panel (the slowest after 6.2 seconds): OK
[PlinkoLive] self-test: practice mode -> 0 calls to OBS (real mode, with a stand-in for OBS: 1944 calls for 1944 presses): OK
[PlinkoLive] self-test: passed (3 of 3 panels as expected, practice mode as expected)
[Sarxina OBS Plugin Manager] self-test: Twitch: 17 of 17 lines read as expected: OK
[Sarxina OBS Plugin Manager] self-test: Twitch account: 5 of 5 profiles read as expected: OK
[Sarxina OBS Plugin Manager] self-test: checks: 16 of 16 messages handled as expected: OK
[Sarxina OBS Plugin Manager] self-test: routing: 28 of 28 steps as expected: OK
[Sarxina OBS Plugin Manager] self-test: Twitch connection: 12 of 12 steps as expected: OK
[Sarxina OBS Plugin Manager] self-test: VTube Studio: 15 of 15 steps as expected: OK
[Sarxina OBS Plugin Manager] self-test: passed
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

Add the folder to the top `CMakeLists.txt` with `add_subdirectory`, and give the plugin a section in `README.md`. It registers with the Sarxina OBS Plugin Manager by itself (`shared\src\plugin-main.cpp`).

## Making a plugin for the Sarxina OBS Plugin Manager

Any OBS plugin can be run by the manager, from this repository or not, without a change to the manager: it knows nothing about any plugin, and everything it shows comes from the plugin. `examples\example-plugin\example-plugin.cpp` is a whole one in about 130 lines.

1. Copy `shared\src\sarxina-obs-plugin-manager.h` into your plugin. It needs only libobs.
2. Fill in a `sarxina_plugin_info`: an id (your module's name), the name and the one sentence shown in the manager's list, and `set_enabled`. The manager calls it with false until the streamer adds your plugin, then with true. Off, act as if you were not installed: no dock, nothing running.
3. Settings, if you have any: `get_properties` describes them with OBS's own properties (`obs_properties_add_bool`, `_int`, `_float`, `_text` and `_list` are drawn), `get_settings` fills in the current values, and `update` gets every change. You keep and save them yourself. The manager draws them behind your plugin's gear.
4. Chat, if you want it: `commands` lists chat commands, each with a name, other names, the lowest role that may use it and a cooldown, and its `run` hears only the ones that passed every check. `chat` gets every message that passed the checks. Either way the text is valid, has no control characters, and is at most 500 characters.
5. VTube Studio, if you want it: `sarxina_obs_plugin_manager_vts_request` sends any request of VTube Studio's public API over the manager's connection, and hands you the answer.
6. From `obs_module_post_load`, call `sarxina_obs_plugin_manager_register(&info)`. If it returns false, the manager is not installed: switch yourself on.

Everything is called on OBS's UI thread, and `info` and everything it points to have to last until OBS exits. To try the example, copy `example-plugin.dll` from `build_x64\examples\example-plugin\RelWithDebInfo\` into `obs-plugins\64bit\` of a test copy of OBS; it shows up in the manager's list.
