//=============================================================================
// ChatSaga/Spriteset.js: The NPCs on the map's spriteset, with name tags that make room for each other.
//=============================================================================

// A sprite for every NPC on the map, kept up to date, and their name tags above everything on the map, lifted where
// they would cover each other. Party members following the player carry name tags too.

(() => {
    "use strict";

    const { npcsOnMap, placeWaitingNowAndThen } = ChatSaga.town;
    const { Sprite_TownNpc } = ChatSaga.sprites;

    /** Whether two name tags, each at its bottom centre, would cover each other. */
    const tagsOverlap = (a, b) =>
        Math.abs(a.x - b.x) < (a.width + b.width) / 2 && a.y > b.y - b.height && a.y - a.height < b.y;

    /** How high a name tag must go to clear every tag already placed, starting from its home. */
    const clearHeight = (tag, placed) => {
        let y = tag.y;
        for (let moved = true, tries = 0; moved && tries < placed.length + 1; tries++) {
            moved = false;
            for (const other of placed) {
                if (tagsOverlap({ ...tag, y }, other)) {
                    y = other.y - other.height;
                    moved = true;
                }
            }
        }
        return y;
    };

    /**
     * Where each name tag goes: at home over its NPC's head, or, where tags would cover each other, lifted above the
     * one in front (lower on screen) until every one can be read.
     */
    const arrangeNameTags = sprites => {
        const tags = sprites.map(sprite => ({ sprite, ...sprite.nameTagHome(), ...sprite.nameTagSize() }));
        tags.sort((a, b) => b.y - a.y);
        const placed = [];
        for (const tag of tags) {
            placed.push({ ...tag, y: clearHeight(tag, placed) });
        }
        return placed;
    };

    const _Spriteset_Map_createCharacters = Spriteset_Map.prototype.createCharacters;
    Spriteset_Map.prototype.createCharacters = function() {
        _Spriteset_Map_createCharacters.call(this);
        this._townNpcSprites = new Map(); // Game_TownNpc -> Sprite_TownNpc
        this._townNameTags = new Sprite(); // over the whole map, tree crowns and roofs included
        this._baseSprite.addChild(this._townNameTags);
        this._partySprites = this.nameFollowers();
        this.updateTownNpcs();
    };

    /** Swaps each follower's sprite for one that carries a name tag, like an NPC's, for the party member it shows. */
    Spriteset_Map.prototype.nameFollowers = function() {
        const followers = this._characterSprites.filter(sprite => sprite._character instanceof Game_Follower);
        return followers.map(sprite => {
            const named = new Sprite_TownNpc(sprite._character);
            this._characterSprites[this._characterSprites.indexOf(sprite)] = named;
            this._tilemap.removeChild(sprite);
            sprite.destroy();
            this._tilemap.addChild(named);
            this._townNameTags.addChild(named.nameTag());
            return named;
        });
    };

    const _Spriteset_Map_update = Spriteset_Map.prototype.update;
    Spriteset_Map.prototype.update = function() {
        _Spriteset_Map_update.call(this);
        this.updateTownNpcs();
        this.updateTownNameTags();
    };

    /** Brings the NPCs' sprites in line with the NPCs on the map: new ones drawn, gone ones taken away. */
    Spriteset_Map.prototype.updateTownNpcs = function() {
        placeWaitingNowAndThen();
        const shown = new Set(npcsOnMap());
        for (const [npc, sprite] of this._townNpcSprites) {
            if (!shown.has(npc)) {
                this.removeTownNpcSprite(npc, sprite);
            }
        }
        for (const npc of shown) {
            if (!this._townNpcSprites.has(npc)) {
                this.addTownNpcSprite(npc);
            }
        }
    };

    /** Puts every name tag where it can be read; a follower with no party member to show has none. */
    Spriteset_Map.prototype.updateTownNameTags = function() {
        const members = this._partySprites.filter(sprite => sprite._character.isVisible() &&
            sprite._character.npcName());
        for (const sprite of this._partySprites) {
            sprite.nameTag().visible = members.includes(sprite) && sprite.nameTag().visible;
        }
        const drawn = [...this._townNpcSprites.values(), ...members].filter(sprite => sprite.isDrawn());
        for (const tag of arrangeNameTags(drawn)) {
            tag.sprite.moveNameTagTo(tag.x, tag.y);
        }
    };

    Spriteset_Map.prototype.addTownNpcSprite = function(npc) {
        const sprite = new Sprite_TownNpc(npc);
        this._townNpcSprites.set(npc, sprite);
        this._characterSprites.push(sprite);
        this._tilemap.addChild(sprite);
        this._townNameTags.addChild(sprite.nameTag());
    };

    Spriteset_Map.prototype.removeTownNpcSprite = function(npc, sprite) {
        this._townNpcSprites.delete(npc);
        this._characterSprites.remove(sprite);
        this._tilemap.removeChild(sprite);
        this._townNameTags.removeChild(sprite.nameTag());
        sprite.destroy();
    };
})();
