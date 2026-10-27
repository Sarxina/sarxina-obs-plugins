//=============================================================================
// ChatSaga/Battle.js: Fights in the field.
//=============================================================================

// Monsters met while walking (each map's encounters, in the editor) fight the party in RPG Maker's own battles. A
// party that loses is not a game over: it wakes up back in town, where the game starts, every member on their feet.
// The monster groups are laid out for RPG Maker's 816x624 screen; on ChatSaga's larger screen they stand as big and in
// the same place relative to the screen's middle and height.

(() => {
    "use strict";

    const LAID_OUT_WIDTH = 816; // the screen the troops' places were set on
    const LAID_OUT_HEIGHT = 624;

    /** How much bigger this screen is than the one the troops were laid out on, by height. */
    const troopScale = () => Graphics.boxHeight / LAID_OUT_HEIGHT;

    // A monster's place, from the troop's layout to this screen: the same distance from the middle, scaled.
    Game_Enemy.prototype.screenX = function() {
        return Graphics.boxWidth / 2 + (this._screenX - LAID_OUT_WIDTH / 2) * troopScale();
    };

    Game_Enemy.prototype.screenY = function() {
        return this._screenY * troopScale();
    };

    // A monster's picture, at the same scale.
    const _Sprite_Enemy_initMembers = Sprite_Enemy.prototype.initMembers;
    Sprite_Enemy.prototype.initMembers = function() {
        _Sprite_Enemy_initMembers.call(this);
        this.scale.set(troopScale(), troopScale());
    };

    /** Brings the beaten party back to town, healed. */
    const wakeUpInTown = () => {
        for (const actor of $gameParty.members()) {
            actor.recoverAll();
        }
        $gamePlayer.reserveTransfer($dataSystem.startMapId, $dataSystem.startX, $dataSystem.startY, 2, 0);
    };

    // A fight with monsters met while walking can be lost; losing sends the party back to town.
    const _Game_Player_executeEncounter = Game_Player.prototype.executeEncounter;
    Game_Player.prototype.executeEncounter = function() {
        const started = _Game_Player_executeEncounter.call(this);
        if (started) {
            BattleManager._canLose = true;
            BattleManager.setEventCallback(result => {
                if (result === 2) {
                    wakeUpInTown();
                }
            });
        }
        return started;
    };

    // An empty party walks about without meeting monsters: there is no one to fight them.
    const _Game_Player_canEncounter = Game_Player.prototype.canEncounter;
    Game_Player.prototype.canEncounter = function() {
        return $gameParty.size() > 0 && _Game_Player_canEncounter.call(this);
    };
})();
