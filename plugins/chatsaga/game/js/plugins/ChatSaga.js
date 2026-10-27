//=============================================================================
// ChatSaga.js
//=============================================================================

/*:
 * @target MZ
 * @plugindesc NPCs that come and go while the game runs, each with a name and something to say.
 * @author Sarxina
 *
 * @help
 * NPCs come and go while the game runs. Each has an id that never changes,
 * a name shown over its head, its dialogue (what it says when the player
 * talks to it), when it last spoke (for 15 seconds after that, a small
 * speech icon shows over its name), its class (from the database, which sets
 * its level-1 stats; one is picked from its id when none is given) and its
 * avatar, a picture's address (a default is drawn when there is none). All
 * but the id can change at any time.
 *
 * The town is a web page. The ChatSaga OBS plugin shows it in its source,
 * and serves it at http://127.0.0.1:4790/ for any browser on this computer;
 * without OBS, chatsaga\town.ps1 serves it there the same way. The page
 * follows the feed at http://127.0.0.1:4790/npcs/events: everyone in town is
 * here, with their names and dialogue, as soon as they come. PUT and DELETE
 * on /npcs/<id> there put NPCs in town, change them and take them out
 * (chatsaga\npc.ps1 sends them by hand).
 *
 * From a script call or another plugin:
 *   ChatSaga.putNpc(id, { name, dialogue, spokeAt, classId, avatar })
 *                      puts an NPC in town, or changes what is given;
 *                      spokeAt is in milliseconds since 1970
 *   ChatSaga.removeNpc(id)
 *   ChatSaga.npcs()    [{ id, name, dialogue, spokeAt, classId, avatar }, ...]
 * An id is 1 to 32 characters and the same in any case; a name is cut to 32
 * characters, and is the id when none is given.
 *
 * Talking to an NPC (facing it and pressing OK, or clicking it) opens the
 * message window with its avatar as the face, its name in the name box and
 * its dialogue, wrapped to the window and cut short past four lines, with
 * the choices View stats and Close beside it. View stats opens the status
 * screen for it: avatar, name, class, level, HP and MP, and its stats.
 * Dialogue written between asterisks, like *lurking*, is shown in italics
 * without them. The NPC turns to the player and stays put until the window
 * closes. An NPC with nothing to say is not talked to. The message window
 * also takes \IT[1] and \IT[0] for italics on and off.
 *
 * An NPC comes in on a free tile that can be walked to from where the player
 * is, on screen if there is room, never under a tree's crown or on the map's
 * outer edge. It strolls about the town at an easy pace, resting in between,
 * on tiles anyone may walk on, away from crowds and never right beside the
 * player, so the player is never boxed in and can always walk up to it. Its
 * name floats over its head, above everything on the map, and names that
 * would cover each other make room. Its look comes from its id, so it keeps
 * it when its name changes. NPCs are not saved.
 *
 * The game opens straight into the town, without the title screen and
 * without the menu button in the corner (Escape still opens the menu), fills
 * the window it is shown in, and keeps going while another window has the
 * focus.
 *
 * The town's parts are in the ChatSaga folder beside this plugin, which
 * loads them.
 */

// The town's parts, in the ChatSaga folder, in the order they build on each other. Each keeps what it offers the others
// under its own name in ChatSaga; putNpc, removeNpc and npcs are for anyone.
(() => {
    "use strict";

    const PARTS = [
        "Identity", "Avatars", "Tiles", "Npc", "Town", "Stroll", "Feed", "Talk", "Status", "Sprites", "Spriteset", "Stage"
    ];

    window.ChatSaga = {};
    for (const part of PARTS) {
        PluginManager.loadScript("ChatSaga/" + part);
    }
})();
