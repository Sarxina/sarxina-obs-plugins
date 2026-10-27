//=============================================================================
// ChatSaga/Town.js: Which NPCs are in town, and where they stand.
//=============================================================================

// Puts NPCs in town, changes them and takes them out (ChatSaga.putNpc, ChatSaga.removeNpc and ChatSaga.npcs), finds
// each a free tile, and keeps them updating and in everyone's way on the map.

(() => {
    "use strict";

    const { LONGEST_ID, npcId, npcName, npcDialogue, spokeTime } = ChatSaga.identity;
    const { isMapReady, isBesidePlayer, isInsideEdge, isUnderCover, isOnScreen, walkableTiles } = ChatSaga.tiles;
    const { Game_TownNpc } = ChatSaga.npc;

    const RETRY_FRAMES = 60; // how often NPCs with no room yet look for a tile again

    const npcs = new Map(); // id -> Game_TownNpc, in the order they came
    let lastTry = -RETRY_FRAMES; // the frame NPCs with no room last looked for a tile

    /** The NPCs standing on the current map. */
    const npcsOnMap = () => [...npcs.values()].filter(npc => npc.isOnCurrentMap());

    /** The NPC standing on this tile of the current map, if any. */
    const npcAt = (x, y) => npcsOnMap().find(npc => npc.pos(x, y));

    /** Whether nothing stands on this tile and it leaves the player room: no event, NPC or player. */
    const isFreeTile = (x, y) =>
        $gameMap.eventsXy(x, y).length === 0 && !npcAt(x, y) && !isBesidePlayer(x, y);

    /** The free tiles a new NPC may stand on, the ones on screen first if there are any. */
    const freeSpots = () => {
        const free = walkableTiles().filter(([x, y]) => isInsideEdge(x, y) && !isUnderCover(x, y) && isFreeTile(x, y));
        const onScreen = free.filter(([x, y]) => isOnScreen(x, y));
        return onScreen.length > 0 ? onScreen : free;
    };

    /** Puts every NPC not yet on the current map onto a free tile of it, while there is room. */
    const placeWaiting = () => {
        const waiting = [...npcs.values()].filter(npc => !npc.isOnCurrentMap());
        if (waiting.length === 0 || !isMapReady()) {
            return;
        }
        const spots = freeSpots();
        for (const npc of waiting) {
            if (spots.length === 0) {
                return;
            }
            const [x, y] = spots.splice(Math.randomInt(spots.length), 1)[0];
            npc.placeAt(x, y);
        }
    };

    /** Places waiting NPCs now and then: at once after an NPC comes, else every RETRY_FRAMES. */
    const placeWaitingNowAndThen = () => {
        if (Graphics.frameCount - lastTry >= RETRY_FRAMES) {
            lastTry = Graphics.frameCount;
            placeWaiting();
        }
    };

    /** Puts an NPC in town, or changes what is given of the one there: any of { name, dialogue, spokeAt }. */
    const putNpc = (givenId, change = {}) => {
        const id = npcId(givenId);
        if (!id) {
            return { status: 400, error: "an id is 1 to " + LONGEST_ID + " characters" };
        }
        const known = npcs.get(id);
        if (known) {
            known.change(change);
            return { status: 200, ...describe(known), added: false };
        }
        const npc = new Game_TownNpc(id, npcName(change.name, id), npcDialogue(change.dialogue), spokeTime(change.spokeAt),
            change.classId, change.avatar || "");
        npcs.set(id, npc);
        lastTry = -RETRY_FRAMES;
        return { status: 201, ...describe(npc), added: true };
    };

    /** Takes the NPC with this id out of town, if it is there. */
    const removeNpc = givenId => {
        const id = npcId(givenId);
        const removed = npcs.delete(id);
        return { status: removed ? 200 : 404, id, removed };
    };

    /** An NPC as callers see it. */
    const describe = npc => ({ id: npc.npcId(), name: npc.npcName(), dialogue: npc.npcDialogue(), spokeAt: npc.spokeAt(),
        classId: npc.classId(), avatar: npc.avatar() });

    /** The NPCs in town, in the order they came. */
    const listNpcs = () => [...npcs.values()].map(describe);

    /** The ids of the NPCs in town. */
    const npcIds = () => [...npcs.keys()];

    // The NPCs on the map stroll whenever the map itself moves on.
    const _Game_Map_update = Game_Map.prototype.update;
    Game_Map.prototype.update = function(sceneActive) {
        _Game_Map_update.call(this, sceneActive);
        for (const npc of npcsOnMap()) {
            npc.update();
        }
    };

    // Characters bump into NPCs.
    const _Game_CharacterBase_isCollidedWithCharacters = Game_CharacterBase.prototype.isCollidedWithCharacters;
    Game_CharacterBase.prototype.isCollidedWithCharacters = function(x, y) {
        const npc = npcAt(x, y);
        return _Game_CharacterBase_isCollidedWithCharacters.call(this, x, y) || (!!npc && npc !== this);
    };

    ChatSaga.town = { npcsOnMap, npcAt, npcIds, placeWaitingNowAndThen };
    Object.assign(ChatSaga, { putNpc, removeNpc, npcs: listNpcs });
})();
