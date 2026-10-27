//=============================================================================
// ChatSaga/Feed.js: The town as the ChatSaga OBS plugin has it.
//=============================================================================

// Follows the feed at http://127.0.0.1:4790/npcs/events, so the town holds whoever the ChatSaga OBS plugin says is in
// it.

(() => {
    "use strict";

    const { npcId } = ChatSaga.identity;
    const { npcIds } = ChatSaga.town;

    const FEED = "http://127.0.0.1:4790/npcs/events";

    /** Makes the town hold exactly these NPCs. */
    const becomeTown = everyone => {
        const ids = new Set(everyone.map(npc => npcId(npc.id)));
        for (const id of npcIds()) {
            if (!ids.has(id)) {
                ChatSaga.removeNpc(id);
            }
        }
        for (const npc of everyone) {
            ChatSaga.putNpc(npc.id, feedChange(npc));
        }
    };

    /** What the feed says of an NPC, as a change to it. */
    const feedChange = npc => ({ name: npc.name, dialogue: npc.dialogue, spokeAt: npc.spokeAt, classId: npc.classId,
        avatar: npc.avatar });

    /** Follows the plugin's feed. It reconnects by itself, and starts again from the whole town each time. */
    const followFeed = () => {
        const feed = new EventSource(FEED);
        const on = (event, take) => feed.addEventListener(event, message => take(JSON.parse(message.data)));
        on("town", data => becomeTown(data.npcs));
        on("npc", npc => ChatSaga.putNpc(npc.id, feedChange(npc)));
        on("gone", npc => ChatSaga.removeNpc(npc.id));
    };

    ChatSaga.feed = { followFeed };
})();
