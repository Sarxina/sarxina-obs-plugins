//=============================================================================
// ChatVillage.js
//=============================================================================

/*:
 * @target MZ
 * @plugindesc NPCs that come and go while the game runs, each with a name and something to say.
 * @author Sarxina
 *
 * @help
 * NPCs come and go while the game runs. Each has an id that never changes,
 * a name shown over its head, and its dialogue: what it says when the player
 * talks to it. The name and the dialogue can change at any time.
 *
 * The town is a web page. The ChatVillage OBS plugin shows it in its source,
 * and serves it at http://127.0.0.1:4790/ for any browser on this computer;
 * without OBS, chatvillage\town.ps1 serves it there the same way. The page
 * follows the feed at http://127.0.0.1:4790/npcs/events: everyone in town is
 * here, with their names and dialogue, as soon as they come. PUT and DELETE
 * on /npcs/<id> there put NPCs in town, change them and take them out
 * (chatvillage\npc.ps1 sends them by hand).
 *
 * From a script call or another plugin:
 *   ChatVillage.putNpc(id, { name, dialogue })  puts an NPC in town, or
 *                                                changes what is given
 *   ChatVillage.removeNpc(id)
 *   ChatVillage.npcs()                          [{ id, name, dialogue }, ...]
 * An id is 1 to 32 characters and the same in any case; a name is cut to 32
 * characters, and is the id when none is given.
 *
 * Talking to an NPC (facing it and pressing OK, or clicking it) opens the
 * message window with its name in the name box and its dialogue, wrapped to
 * the window and over as many pages as it needs. Dialogue written between
 * asterisks, like *lurking*, is shown in italics without them. The NPC turns
 * to the player and stays put until the window closes. An NPC with nothing
 * to say is not talked to. The message window also takes \IT[1] and \IT[0]
 * for italics on and off.
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
 * The game opens straight into the town, without the title screen, fills
 * the window it is shown in, and keeps going while another window has the
 * focus.
 */

(() => {
    "use strict";

    const FEED = "http://127.0.0.1:4790/npcs/events";
    const LONGEST_ID = 32;
    const LONGEST_NAME = 32;
    const LONGEST_DIALOGUE = 500; // as long as a chat message gets
    const NAME_FONT_SIZE = 20;
    const NAME_GAP = 10; // pixels between the top of an NPC's frame and its name
    const NAME_EASE = 0.25; // how quickly a name slides to where it makes room
    const RETRY_FRAMES = 60; // how often NPCs with no room yet look for a tile again
    const WALK_SPEED = 3; // RPG Maker's move speed: 3 is a stroll
    const STROLL_STEPS = [1, 4]; // steps in one stroll, fewest and most
    const REST_FRAMES = [120, 480]; // the rest between strolls, shortest and longest
    const CROWD_RADIUS = 2; // tiles: NPCs this close count as a crowd

    // The Time Fantasy character sheets NPCs are dressed from: 8 characters each.
    const LOOKS = [
        "npc1", "npc2", "npc3", "npc4",
        "chara1", "chara2", "chara3", "chara4", "chara5",
        "children_mv", "military1", "military2", "military3", "bonus1"
    ];

    /** A whole number from the first to the second of the pair, both included. */
    const randomBetween = ([low, high]) => low + Math.randomInt(high - low + 1);

    //-------------------------------------------------------------------------
    // Ids, names and dialogue

    /** Text as it is shown: one line, with nothing that hides or turns text around. */
    const plainText = text =>
        String(text ?? "")
            .replace(/[\u0000-\u001f\u007f-\u009f\u200b-\u200f\u202a-\u202e\u2066-\u2069\ufeff]/g, "")
            .trim();

    /** At most that many characters of the text. */
    const cutTo = (text, longest) => [...text].slice(0, longest).join("");

    /** The id an NPC is known by, or "" if what was given cannot be one. */
    const npcId = given => {
        const id = plainText(given);
        return [...id].length <= LONGEST_ID ? id.toLowerCase() : "";
    };

    /** The name shown over an NPC: its id when none is given. */
    const npcName = (given, id) => cutTo(plainText(given), LONGEST_NAME) || id;

    /** What an NPC says when talked to; "" for nothing. */
    const npcDialogue = given => cutTo(plainText(given), LONGEST_DIALOGUE);

    /** The character sheet and index an NPC with this id wears. */
    const lookFor = id => {
        let hash = 2166136261;
        for (const c of id) {
            hash = Math.imul(hash ^ c.codePointAt(0), 16777619) >>> 0;
        }
        return { characterName: LOOKS[hash % LOOKS.length], characterIndex: (hash >>> 8) % 8 };
    };

    //-------------------------------------------------------------------------
    // Game_TownNpc

    /** One NPC on the map, strolling about the town, with a name and something to say. */
    class Game_TownNpc extends Game_Character {
        initialize(id, name, dialogue) {
            super.initialize();
            this._npcId = id;
            this._npcName = name;
            this._npcDialogue = dialogue;
            this._townMapId = 0; // the map it was put on; 0 until it is
            this._stepsLeft = 0; // of the stroll under way
            this._restFrames = randomBetween(REST_FRAMES);
            const look = lookFor(id);
            this.setImage(look.characterName, look.characterIndex);
            this.setDirection(2);
            this.setMoveSpeed(WALK_SPEED);
        }

        npcId() {
            return this._npcId;
        }

        npcName() {
            return this._npcName;
        }

        npcDialogue() {
            return this._npcDialogue;
        }

        /** Takes what is given of a new name and new dialogue. */
        change({ name, dialogue }) {
            if (name !== undefined) {
                this._npcName = npcName(name, this._npcId);
            }
            if (dialogue !== undefined) {
                this._npcDialogue = npcDialogue(dialogue);
            }
        }

        /** Whether it stands on the map shown now. */
        isOnCurrentMap() {
            return this._townMapId === $gameMap.mapId();
        }

        placeAt(x, y) {
            this._townMapId = $gameMap.mapId();
            this.locate(x, y);
        }

        update() {
            super.update();
            this.updateStroll();
        }

        /** Walks a few steps now and then, and rests in between; never while the player is talking to it. */
        updateStroll() {
            if (this.isMoving() || isTalkingTo(this)) {
                return;
            }
            if (this._stepsLeft > 0) {
                this.stepSomewhere();
            } else if (--this._restFrames <= 0) {
                this._stepsLeft = randomBetween(STROLL_STEPS);
                this._restFrames = randomBetween(REST_FRAMES);
            }
        }

        /** Takes the stroll's next step, or ends the stroll when there is nowhere good to go. */
        stepSomewhere() {
            const direction = chooseStep(this);
            this._stepsLeft = direction ? this._stepsLeft - 1 : 0;
            if (direction) {
                this.moveStraight(direction);
            }
        }

        /** NPCs never walk into the player. */
        isCollidedWithCharacters(x, y) {
            return super.isCollidedWithCharacters(x, y) || $gamePlayer.pos(x, y);
        }
    }

    //-------------------------------------------------------------------------
    // The town: which NPCs there are and where they stand

    const npcs = new Map(); // id -> Game_TownNpc, in the order they came
    let lastTry = -RETRY_FRAMES; // the frame NPCs with no room last looked for a tile

    /** Whether a map is loaded that NPCs can be put on. */
    const isMapReady = () => !!($gameMap && $gameMap.mapId() > 0 && $dataMap);

    /** The NPC standing on this tile of the current map, if any. */
    const npcAt = (x, y) =>
        [...npcs.values()].find(npc => npc.isOnCurrentMap() && npc.pos(x, y));

    /** Whether the tile is the player's or right beside it, where NPCs would box the player in. */
    const isBesidePlayer = (x, y) => Math.abs(x - $gamePlayer.x) + Math.abs(y - $gamePlayer.y) <= 1;

    /** Whether nothing stands on this tile and it leaves the player room: no event, NPC or player. */
    const isFreeTile = (x, y) =>
        $gameMap.eventsXy(x, y).length === 0 && !npcAt(x, y) && !isBesidePlayer(x, y);

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

    //-------------------------------------------------------------------------
    // Where a strolling NPC steps next

    /** The tile one step from the NPC in that direction. */
    const stepTarget = (npc, direction) =>
        [$gameMap.roundXWithDirection(npc.x, direction), $gameMap.roundYWithDirection(npc.y, direction)];

    /** How many other NPCs are close to the tile. */
    const crowdAround = (x, y, self) =>
        [...npcs.values()].filter(npc => npc !== self && npc.isOnCurrentMap() &&
            Math.abs(npc.x - x) + Math.abs(npc.y - y) <= CROWD_RADIUS).length;

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

    // The NPCs on the map stroll whenever the map itself moves on.
    const _Game_Map_update = Game_Map.prototype.update;
    Game_Map.prototype.update = function(sceneActive) {
        _Game_Map_update.call(this, sceneActive);
        for (const npc of npcs.values()) {
            if (npc.isOnCurrentMap()) {
                npc.update();
            }
        }
    };

    //-------------------------------------------------------------------------
    // Putting NPCs in town, changing them and taking them out

    /** Puts an NPC in town, or changes what is given of the one there: { name, dialogue }, either or both. */
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
        const npc = new Game_TownNpc(id, npcName(change.name, id), npcDialogue(change.dialogue));
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
    const describe = npc => ({ id: npc.npcId(), name: npc.npcName(), dialogue: npc.npcDialogue() });

    /** The NPCs in town, in the order they came. */
    const listNpcs = () => [...npcs.values()].map(describe);

    window.ChatVillage = { putNpc, removeNpc, npcs: listNpcs };

    //-------------------------------------------------------------------------
    // The feed: the town as the ChatVillage OBS plugin has it

    /** Makes the town hold exactly these NPCs. */
    const becomeTown = everyone => {
        const ids = new Set(everyone.map(npc => npcId(npc.id)));
        for (const id of [...npcs.keys()]) {
            if (!ids.has(id)) {
                removeNpc(id);
            }
        }
        for (const npc of everyone) {
            putNpc(npc.id, { name: npc.name, dialogue: npc.dialogue });
        }
    };

    /** Follows the plugin's feed. It reconnects by itself, and starts again from the whole town each time. */
    const followFeed = () => {
        const feed = new EventSource(FEED);
        const on = (event, take) => feed.addEventListener(event, message => take(JSON.parse(message.data)));
        on("town", data => becomeTown(data.npcs));
        on("npc", npc => putNpc(npc.id, { name: npc.name, dialogue: npc.dialogue }));
        on("gone", npc => removeNpc(npc.id));
    };

    followFeed();

    //-------------------------------------------------------------------------
    // Characters bump into NPCs

    const _Game_CharacterBase_isCollidedWithCharacters = Game_CharacterBase.prototype.isCollidedWithCharacters;
    Game_CharacterBase.prototype.isCollidedWithCharacters = function(x, y) {
        const npc = npcAt(x, y);
        return _Game_CharacterBase_isCollidedWithCharacters.call(this, x, y) || (!!npc && npc !== this);
    };

    //-------------------------------------------------------------------------
    // Talking to an NPC

    let talkingTo = null; // the NPC whose dialogue the message window shows

    /** Whether the player is talking to this NPC now. */
    const isTalkingTo = npc => npc === talkingTo && $gameMessage.isBusy();

    /** How dialogue reads: its text, and whether it is in italics (written *like this*). */
    const dialogueLook = dialogue => {
        const italic = /^\*[^*]+\*$/.test(dialogue);
        return { text: italic ? dialogue.slice(1, -1) : dialogue, italic };
    };

    /** The width the message window has for a line of text. */
    const messageLineWidth = () => Graphics.boxWidth - $gameSystem.windowPadding() * 2 - 16;

    /** A blank bitmap set up in the message window's font, to measure text with. */
    const messageRuler = italic => {
        const ruler = new Bitmap(1, 1);
        ruler.fontFace = $gameSystem.mainFontFace();
        ruler.fontSize = $gameSystem.mainFontSize();
        ruler.fontItalic = italic;
        return ruler;
    };

    /** A word too wide for a line, broken into pieces that each fit. */
    const breakWord = (word, width, ruler) => {
        const pieces = [""];
        for (const c of word) {
            const piece = pieces[pieces.length - 1] + c;
            if (ruler.measureTextWidth(piece) > width && pieces[pieces.length - 1]) {
                pieces.push(c);
            } else {
                pieces[pieces.length - 1] = piece;
            }
        }
        return pieces;
    };

    /** The text in lines that each fit the width, breaking between words, and inside words too long for a line. */
    const wrapText = (text, width, italic) => {
        const ruler = messageRuler(italic);
        const words = text.split(/\s+/).filter(Boolean).flatMap(word => breakWord(word, width, ruler));
        const lines = [];
        for (const word of words) {
            const line = lines.length ? lines[lines.length - 1] + " " + word : "";
            if (lines.length && ruler.measureTextWidth(line) <= width) {
                lines[lines.length - 1] = line;
            } else {
                lines.push(word);
            }
        }
        ruler.destroy();
        return lines;
    };

    /** The text as the message window shows it as it is: its own escape codes read as plain backslashes. */
    const asMessageText = text => text.replace(/\\/g, "\\\\");

    /** Opens the message window with what the NPC says, its name in the name box. */
    const showDialogue = npc => {
        const look = dialogueLook(npc.npcDialogue());
        $gameMessage.setSpeakerName(asMessageText(npc.npcName()));
        for (const line of wrapText(look.text, messageLineWidth(), look.italic)) {
            $gameMessage.add((look.italic ? "\\IT[1]" : "") + asMessageText(line));
        }
    };

    /** Starts a conversation with the NPC: it turns to the player and has its say. */
    const talkTo = npc => {
        talkingTo = npc;
        npc.turnTowardPlayer();
        showDialogue(npc);
    };

    /** The NPC on the tile the player faces, if it has something to say. */
    const npcFacingPlayer = () => {
        const x = $gameMap.roundXWithDirection($gamePlayer.x, $gamePlayer.direction());
        const y = $gameMap.roundYWithDirection($gamePlayer.y, $gamePlayer.direction());
        const npc = npcAt(x, y);
        return npc && npc.npcDialogue() ? npc : null;
    };

    // The OK button, or a click on the tile in front, talks to the NPC there when no event takes it first.
    const _Game_Player_checkEventTriggerThere = Game_Player.prototype.checkEventTriggerThere;
    Game_Player.prototype.checkEventTriggerThere = function(triggers) {
        _Game_Player_checkEventTriggerThere.call(this, triggers);
        const npc = triggers.includes(0) && !$gameMap.isAnyEventStarting() && !$gameMessage.isBusy()
            ? npcFacingPlayer()
            : null;
        if (npc) {
            talkTo(npc);
        }
    };

    //-------------------------------------------------------------------------
    // Italics in the message window: \IT[1] on, \IT[0] off

    const _Window_Base_processEscapeCharacter = Window_Base.prototype.processEscapeCharacter;
    Window_Base.prototype.processEscapeCharacter = function(code, textState) {
        if (code === "IT") {
            this.contents.fontItalic = this.obtainEscapeParam(textState) === 1;
        } else {
            _Window_Base_processEscapeCharacter.call(this, code, textState);
        }
    };

    const _Window_Base_resetFontSettings = Window_Base.prototype.resetFontSettings;
    Window_Base.prototype.resetFontSettings = function() {
        _Window_Base_resetFontSettings.call(this);
        this.contents.fontItalic = false;
    };

    //-------------------------------------------------------------------------
    // Sprite_TownNpc

    /** An NPC as drawn: its character, and its name tag, which floats in the spriteset's layer of name tags. */
    class Sprite_TownNpc extends Sprite_Character {
        initialize(npc) {
            super.initialize(npc);
            this._nameTag = new Sprite();
            this._nameTag.anchor.set(0.5, 1);
            this._nameTag.visible = false; // until it has a place
            this._shownName = null;
            this._nameTagPlaced = false; // whether the tag has had a place yet, or still has to jump to one
        }

        nameTag() {
            return this._nameTag;
        }

        /** Whether the NPC's picture has loaded, so that its head, and the name tag's home, can be found. */
        isDrawn() {
            return !!this.bitmap && this.bitmap.isReady();
        }

        update() {
            super.update();
            if (this._shownName !== this._character.npcName()) {
                this.drawName(this._character.npcName());
            }
        }

        /** Where the name tag sits when nothing is in its way: just over the NPC's head. */
        nameTagHome() {
            return { x: this.x, y: this.y - this.patternHeight() + NAME_GAP + NAME_FONT_SIZE };
        }

        /** The name tag's size on screen, without the room its outline takes. */
        nameTagSize() {
            const bitmap = this._nameTag.bitmap;
            return bitmap ? { width: bitmap.width - 8, height: NAME_FONT_SIZE + 4 } : { width: 0, height: 0 };
        }

        /** Slides the name tag toward where it makes room for the others, or puts it there at once the first time. */
        moveNameTagTo(x, y) {
            this._nameTag.x = x;
            this._nameTag.y = this._nameTagPlaced ? this._nameTag.y + (y - this._nameTag.y) * NAME_EASE : y;
            this._nameTag.visible = true;
            this._nameTagPlaced = true;
        }

        /** Shows this name over the NPC's head, in place of what was there. */
        drawName(name) {
            if (this._nameTag.bitmap) {
                this._nameTag.bitmap.destroy();
            }
            const width = Math.ceil(this.measureName(name)) + 12;
            const height = NAME_FONT_SIZE + 12;
            const bitmap = this.nameBitmap(width, height);
            bitmap.outlineWidth = 4;
            bitmap.drawText(name, 0, 0, width, height, "center");
            this._nameTag.bitmap = bitmap;
            this._shownName = name;
        }

        /** A blank bitmap set up in the name tag's font. */
        nameBitmap(width, height) {
            const bitmap = new Bitmap(width, height);
            bitmap.fontFace = $gameSystem.mainFontFace();
            bitmap.fontSize = NAME_FONT_SIZE;
            return bitmap;
        }

        /** How wide a name is in the name tag's font. */
        measureName(name) {
            const ruler = this.nameBitmap(1, 1);
            const width = ruler.measureTextWidth(name);
            ruler.destroy();
            return width;
        }

        destroy(options) {
            if (this._nameTag.bitmap) {
                this._nameTag.bitmap.destroy();
            }
            this._nameTag.destroy();
            super.destroy(options);
        }
    }

    //-------------------------------------------------------------------------
    // Name tags that make room for each other

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

    //-------------------------------------------------------------------------
    // Spriteset_Map: a sprite for every NPC on the map, kept up to date, with the name tags above everything

    const _Spriteset_Map_createCharacters = Spriteset_Map.prototype.createCharacters;
    Spriteset_Map.prototype.createCharacters = function() {
        _Spriteset_Map_createCharacters.call(this);
        this._townNpcSprites = new Map(); // Game_TownNpc -> Sprite_TownNpc
        this._townNameTags = new Sprite(); // over the whole map, tree crowns and roofs included
        this._baseSprite.addChild(this._townNameTags);
        this.updateTownNpcs();
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
        const shown = new Set([...npcs.values()].filter(npc => npc.isOnCurrentMap()));
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

    /** Puts every name tag where it can be read. */
    Spriteset_Map.prototype.updateTownNameTags = function() {
        const drawn = [...this._townNpcSprites.values()].filter(sprite => sprite.isDrawn());
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

    //-------------------------------------------------------------------------
    // The town keeps going while another window has the focus, as it does on stream

    SceneManager.isGameActive = function() {
        return true;
    };

    //-------------------------------------------------------------------------
    // The town fills the browser window or OBS source it is shown in, whatever its size

    Graphics._defaultStretchMode = function() {
        return true;
    };

    //-------------------------------------------------------------------------
    // The game opens straight into the town

    Scene_Boot.prototype.startNormalGame = function() {
        this.checkPlayerLocation();
        DataManager.setupNewGame();
        SceneManager.goto(Scene_Map);
    };
})();
