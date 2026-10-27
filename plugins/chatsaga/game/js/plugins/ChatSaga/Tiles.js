//=============================================================================
// ChatSaga/Tiles.js: What the current map's tiles are like for an NPC.
//=============================================================================

// Which tiles can be walked to, are beside the player, under cover, on the map's edge or on screen.

(() => {
    "use strict";

    /** Whether the town is the map loaded now: NPCs live there, the map the game opens on, and wait while the player
     * is elsewhere. */
    const isMapReady = () => !!($gameMap && $dataMap && $gameMap.mapId() === $dataSystem.startMapId);

    /** Whether the tile is the player's or right beside it, where NPCs would box the player in. */
    const isBesidePlayer = (x, y) => Math.abs(x - $gamePlayer.x) + Math.abs(y - $gamePlayer.y) <= 1;

    /** Whether the tile is away from the map's outer edge, where an NPC's name would be cut off. */
    const isInsideEdge = (x, y) => x > 0 && y > 0 && x < $gameMap.width() - 1 && y < $gameMap.height() - 1;

    /** Whether something drawn over characters, such as a tree's crown, covers the tile. */
    const isUnderCover = (x, y) => {
        const flags = $gameMap.tilesetFlags();
        return $gameMap.layeredTiles(x, y).some(tileId => tileId > 0 && (flags[tileId] & 0x10) !== 0);
    };

    /** Whether the tile is inside the part of the map on screen. */
    const isOnScreen = (x, y) => {
        const left = $gameMap.displayX();
        const top = $gameMap.displayY();
        return x >= left && y >= top &&
            x < left + $gameMap.screenTileX() - 1 && y < top + $gameMap.screenTileY() - 1;
    };

    /** Whether the map lets a character step from one tile to the next. */
    const canStep = (x, y, d, nx, ny) =>
        $gameMap.isValid(nx, ny) &&
        $gameMap.isPassable(x, y, d) &&
        $gameMap.isPassable(nx, ny, 10 - d);

    /** Every tile that can be walked to from where the player stands. */
    const walkableTiles = () => {
        const start = [$gamePlayer.x, $gamePlayer.y];
        const seen = new Set([start.join(",")]);
        const tiles = [start];
        for (let i = 0; i < tiles.length; i++) {
            const [x, y] = tiles[i];
            for (const d of [2, 4, 6, 8]) {
                const nx = $gameMap.roundXWithDirection(x, d);
                const ny = $gameMap.roundYWithDirection(y, d);
                const key = nx + "," + ny;
                if (!seen.has(key) && canStep(x, y, d, nx, ny)) {
                    seen.add(key);
                    tiles.push([nx, ny]);
                }
            }
        }
        return tiles;
    };

    ChatSaga.tiles = { isMapReady, isBesidePlayer, isInsideEdge, isUnderCover, isOnScreen, walkableTiles };
})();
