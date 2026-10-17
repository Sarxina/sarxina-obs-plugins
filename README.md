# Sarxina OBS Plugins

## SlingLive

The only OBS plugin that makes you hit the right button with a slingshot to Record or Go Live.

![SlingLive in OBS](docs/slinglive-demo.gif)

## PlinkoLive

The only OBS plugin that makes you drop your head down a Plinko board to Record or Go Live.

## PlinkoToggle

A Plinko board for your scene that chat plays: a chat command or a channel point redeem drops a ball, and the VTube Studio toggle it lands on is switched. Every hotkey of the model loaded in VTube Studio gets a slot, and the board grows with them. It needs the Sarxina OBS Plugin Manager, connected to VTube Studio and reading the chat of the Twitch account OBS is connected to. Pick the command or the reward, and the cooldown, behind its gear in the manager. A reward has to have **Require Viewer to Enter Text** on: that is the only way Twitch shows a redeem in chat.

## Sarxina Parametric EQ

An audio filter laid out like Adobe Audition's Parametric Equalizer: a high-pass, a low shelf, five bands, a high shelf and a low-pass. Add it under **Filters** on any audio source and press **Open EQ curve** to drag the bands on the curve or type their numbers.

## Sarxina Noise Gate

A noise gate for a voice: the voice passes untouched, the room under it is turned far down. It opens and closes at two different levels, holds open through word tails, fades out instead of cutting, and looks a few milliseconds ahead so words never start clipped.

## Sarxina Hard Limiter

A look-ahead limiter like Adobe Audition's Hard Limiter: input boost, maximum amplitude, look-ahead, release and linked channels. No sample ever goes over the maximum amplitude.

## Sarxina OBS Plugin Manager

One dock for all of them: add the Sarxina plugins you want to OBS, change each one's settings behind its gear, and let them share one VTube Studio connection and the chat of the Twitch account OBS is connected to (Settings > Stream > Connect Account). Every plugin also works without it.

## Install

Needs OBS Studio 32.2 or newer on Windows.

1. Close OBS.
2. Download the plugin's zip from [Releases](../../releases).
3. Extract it into your OBS Studio folder, usually `C:\Program Files\obs-studio`.
4. Open OBS and tick the plugin under **Docks**. With the Sarxina OBS Plugin Manager installed, add the plugin in the manager's list first. PlinkoToggle is a source instead: in **Sources**, press **+** and pick PlinkoToggle. Sarxina Parametric EQ, Sarxina Noise Gate and Sarxina Hard Limiter are filters: right-click an audio source, **Filters**, then **+**.
