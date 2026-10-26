# Sarxina OBS Plugins

OBS plugins I made for my streams: a few silly ones and a few useful ones. You install the **Sarxina OBS Plugin Manager** once, and then add or remove the others from inside OBS.

Windows only, OBS Studio 32.2 or newer.

## Installing

1. Close OBS.
2. Download the manager's zip from [the latest release](../../releases/latest) and extract it into your OBS folder (usually `C:\Program Files\obs-studio`).
3. Open OBS and tick **Sarxina OBS Plugin Manager** in the **Docks** menu.
4. In the dock, pick a plugin from the list, hit **Install**, then restart OBS. **Uninstall** and **Update** work the same way.

When a new version of the manager itself is out, an up arrow shows at the top of its dock. Click it and restart OBS. If OBS is in Program Files, Windows asks first whether the manager may change files there.

Each plugin's settings are behind the gear next to it in the dock. (This isn't OBS's own plugin manager under Tools. It only handles these plugins.)

If you'd rather not use the manager, every plugin's zip is on [Releases](../../releases). Close OBS and extract the zip into your OBS folder. Anything that uses Twitch or VTube Studio still needs the manager, though.

## Connecting Twitch

The manager holds one Twitch connection, and every plugin uses that same connection for chat, channel point redeems and the list of who's in chat. OBS's own Twitch account (Settings > Stream) isn't used.

1. In the manager's dock, hit **Connect** next to **Twitch**.
2. Your browser opens twitch.tv/activate, and the dock shows a code. Enter the code there and allow the app.
3. The dock then says **Connected as** your channel, and the line under it says whose chat it's reading.

You only do this once. The manager remembers the connection and keeps it fresh. Your tokens stay in the manager's settings folder and never go into OBS's log.

When an update adds a plugin that needs a new Twitch permission, the dock says **Needs reconnecting**. Hit **Connect** again and enter the new code. ChatSaga is one of these: it has to see who's in your chat.

Channel point rewards only work on affiliate or partner channels. That's Twitch's rule, not the plugins'.

## Connecting VTube Studio

PlinkoToggle and JobApplication talk to VTube Studio through the manager.

1. In VTube Studio, open Settings and turn on **Start API (allow plugins)**. The port it shows is usually 8001.
2. In the manager's dock, put that port next to **VTube Studio** and hit **Connect**.
3. VTube Studio pops up a box asking whether to allow **Sarxina OBS Plugin Manager**. Click **Allow**.

VTube Studio only asks the first time. After that the manager reconnects on its own whenever OBS and VTube Studio are both running, and it never pops the box up unless you hit Connect. If you said no by mistake, hit **Connect** again.

## The plugins

| Plugin | What it is | What it does |
|---|---|---|
| **SlingLive** | Dock | Fling your head at OBS's buttons with a slingshot. Good luck hitting the right one. |
| **PlinkoLive** | Dock | Drop your head down a Plinko board to decide whether you Record, Go Live or Exit. |
| **PlinkoToggle** | Source | A Plinko board where chat's drops switch your VTube Studio toggles. |
| **JobApplication** | VTube Studio item | A job application on your model that chat fills out, one line of work experience per redeem. |
| **ChatSaga** | Source | Your chat as a JRPG town: everyone in chat is an NPC with a class, stats and their last message. |
| **VoiceGoodifier** | Audio filters | A simple plug and play filter chain for your mic: a noise gate, a drag-the-curve EQ and a hard limiter. |

![SlingLive in OBS](docs/slinglive-demo.gif)

## Where each one shows up

- **SlingLive, PlinkoLive:** tick them in the **Docks** menu.
- **PlinkoToggle:** in **Sources**, press **+** and pick **PlinkoToggle**. Behind its gear in the manager you choose between a chat command and one of your channel's redeems, or hit **Create a new redeem**. A redeem needs **Require Viewer to Enter Text** on, or Twitch won't show it in chat. The button turns that on for you.
- **JobApplication:** nothing to add to a scene. It appears on your model in VTube Studio. Set its hotkey in **Settings > Hotkeys**. Behind its gear, pick the redeem viewers use to add a line (or switch to a `!jobapp <text>` command), change the paper's size, or hit **Clear the application** to wipe it.
  - The first time the paper comes up, VTube Studio shows a tip about moving Live2D items, and the paper waits until you close it. Tick **Don't show again** and hit **OK**.
  - The paper goes into VTube Studio's `Items` folder as `SarxinaJobApplication`. If VTube Studio can't load it, a flat picture shows instead, and VTube Studio asks once whether the manager may load custom images. Hit **Allow**.
- **ChatSaga:** right-click the **Sources** list and pick **Sarxina Source > ChatSaga**. The town gets added to the scene you're editing, sized to fit. You can also add it the usual way with **+**.
  - It needs Twitch connected to see who's in chat. Without the connection, only people who type show up.
  - Bots like Nightbot and StreamElements are left out, and you're the player in the middle, not an NPC.
  - While OBS is running, the town is also a web page at http://127.0.0.1:4790/ if you want to watch it in a browser.
- **VoiceGoodifier:** right-click your mic, pick **Filters**, then **+**, and choose the voice chain or one of the three filters.
