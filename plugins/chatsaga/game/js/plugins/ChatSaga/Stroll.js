//=============================================================================
// ChatSaga/Stroll.js: Where a strolling NPC steps next.
//=============================================================================

// An NPC steps on along its way rather than turning, away from crowds, inside the map's edge and never beside the
// player.

(() => {
    "use strict";

    const { isBesidePlayer, isInsideEdge } = ChatSaga.tiles;
    const { npcsOnMap } = ChatSaga.town;

    const CROWD_RADIUS = 2; // tiles: NPCs this close count as a crowd

    /** The tile one step from the NPC in that direction. */
    const stepTarget = (npc, direction) =>
        [$gameMap.roundXWithDirection(npc.x, direction), $gameMap.roundYWithDirection(npc.y, direction)];

    /** How many other NPCs are close to the tile. */
    const crowdAround = (x, y, self) =>
        npcsOnMap().filter(npc => npc !== self && Math.abs(npc.x - x) + Math.abs(npc.y - y) <= CROWD_RADIUS).length;

    /** Whether the NPC may step that way: onto a tile it can walk to, inside the map's edge, not beside the player. */
    const isGoodStep = (npc, direction) => {
        const [x, y] = stepTarget(npc, direction);
        return npc.canPass(npc.x, npc.y, direction) && isInsideEdge(x, y) && !isBesidePlayer(x, y);
    };

    /** How much the NPC wants to step that way: on along its way rather than turning, and away from crowds. */
    const stepWish = (npc, direction) => {
        const [x, y] = stepTarget(npc, direction);
        return (direction === npc.direction() ? 3 : 1) / (1 + crowdAround(x, y, npc));
    };

    /** The direction of the NPC's next step, picked at random as much as it wants each, or 0 for none. */
    const chooseStep = npc => {
        const options = [2, 4, 6, 8].filter(direction => isGoodStep(npc, direction));
        const wishes = options.map(direction => stepWish(npc, direction));
        let pick = Math.random() * wishes.reduce((sum, wish) => sum + wish, 0);
        for (let i = 0; i < options.length; i++) {
            pick -= wishes[i];
            if (pick < 0) {
                return options[i];
            }
        }
        return 0;
    };

    ChatSaga.stroll = { chooseStep };
})();
