# Sarxina OBS Plugins

OBS plugins I made for my streams: a few silly ones and a few useful ones. You install the **Sarxina OBS Plugin Manager** once, and then add or remove the others from inside OBS.

Windows only, OBS Studio 32.2 or newer.

## Installing

1. Close OBS.
2. Download [**Sarxina-OBS-Plugin-Manager-Setup.exe**](../../releases/latest/download/Sarxina-OBS-Plugin-Manager-Setup.exe) and run it. If Windows says "Windows protected your PC", click **More info**, then **Run anyway** (the installer isn't code-signed yet).
3. Open OBS and tick **Sarxina OBS Plugin Manager** in the **Docks** menu.
4. In the dock, pick a plugin from the list, hit **Install**, then restart OBS. **Uninstall** and **Update** work the same way.

The installer checks that your OBS is 32.2 or newer and puts the manager where OBS loads plugins from. To remove the manager, uninstall it from Windows' **Installed apps** like any other program. For a portable OBS, download the manager's zip from [the latest release](../../releases/latest) instead and extract it into your OBS folder.

When a new version of the manager itself is out, an up arrow shows at the top of its dock. Click it and restart OBS. Windows may ask first whether the manager may change its files.

Click ⚙️ next to a plugin for its settings.

If you'd rather not use the manager, every plugin's zip is on [Releases](../../releases). Close OBS and extract the zip into your OBS folder. Anything that uses Twitch or VTube Studio still needs the manager, though.

## Connections

### Twitch

Every plugin uses the manager's one Twitch connection for chat, channel point redeems and who's in chat.

1. In the manager's dock, hit **Connect** next to **Twitch**.
2. Enter the code the dock shows on the twitch.tv/activate page that opens, and allow the app.

### VTube Studio

PlinkoToggle and JobApplication move your model through the manager.

1. In VTube Studio's settings, turn on **Start API (allow plugins)**.
2. In the manager's dock, hit **Connect** next to **VTube Studio**, then **Allow** in VTube Studio.

## The plugins

| Plugin | What it is | What it does |
|---|---|---|
| **SlingLive** | Dock | Fling your head at OBS's buttons with a slingshot. Good luck hitting the right one. |
| **PlinkoLive** | Dock | Drop your head down a Plinko board to decide whether you Record, Go Live or Exit. |
| **PlinkoToggle** | Source | A Plinko board where chat's drops switch your VTube Studio toggles. |
| **JobApplication** | VTube Studio item | A job application on your model that chat fills out, one line of work experience per redeem. |
| **ChatSaga** | Source | Your chat as a JRPG town: everyone in chat is an NPC with a class, stats and their last message. |
| **VoiceGoodifier** | Audio filters | A simple plug and play filter chain for your mic: a noise gate, a drag-the-curve EQ and a hard limiter. |
| **KeepTalking** | Source | Go quiet too long and a countdown shows on stream. At zero, your game closes. |
| **WheelOfYap** | Source | A giant wheel of topics. Chat spins it with a channel point redeem, or pays more to force their own topic onto it. |
