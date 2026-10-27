//=============================================================================
// ChatSaga/Sprites.js: How an NPC is drawn, with its name tag and speech icon.
//=============================================================================

// Sprite_TownNpc: the NPC's character, its name over its head and, while it is speaking, a small speech bubble over
// its name.

(() => {
    "use strict";

    const NAME_FONT_SIZE = 20;
    const NAME_GAP = 10; // pixels between the top of an NPC's frame and its name
    const NAME_EASE = 0.25; // how quickly a name slides to where it makes room
    const ICON_WIDTH = 30;
    const ICON_HEIGHT = 26;

    let speechIcon = null;

    /** The speech icon's picture, drawn once and shared by every NPC. */
    const speechIconBitmap = () => {
        if (!speechIcon) {
            speechIcon = new Bitmap(ICON_WIDTH, ICON_HEIGHT);
            drawSpeechBubble(speechIcon.context);
            speechIcon.baseTexture.update();
        }
        return speechIcon;
    };

    /** Draws a rounded white bubble with a tail at its bottom left, and three dots in it. */
    const drawSpeechBubble = context => {
        context.save();
        context.lineWidth = 2;
        context.strokeStyle = "#2b2233";
        context.fillStyle = "#ffffff";
        context.beginPath();
        context.roundRect(2, 2, ICON_WIDTH - 4, ICON_HEIGHT - 9, 6);
        context.moveTo(8, ICON_HEIGHT - 8);
        context.lineTo(6, ICON_HEIGHT - 2);
        context.lineTo(14, ICON_HEIGHT - 8);
        context.fill();
        context.stroke();
        context.fillStyle = "#2b2233";
        for (const x of [9, 15, 21]) {
            context.beginPath();
            context.arc(x, 10, 2, 0, Math.PI * 2);
            context.fill();
        }
        context.restore();
    };

    /** An NPC as drawn: its character, and its name tag, which floats in the spriteset's layer of name tags. */
    class Sprite_TownNpc extends Sprite_Character {
        initialize(npc) {
            super.initialize(npc);
            this._nameTag = new Sprite();
            this._nameTag.anchor.set(0.5, 1);
            this._nameTag.visible = false; // until it has a place
            this._speechIcon = new Sprite(speechIconBitmap());
            this._speechIcon.anchor.set(0.5, 1);
            this._speechIcon.visible = false;
            this._nameTag.addChild(this._speechIcon);
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
            this.updateSpeechIcon();
        }

        /** Shows the speech icon over the name while the NPC is speaking, bobbing gently. */
        updateSpeechIcon() {
            this._speechIcon.visible = this._character.isSpeaking();
            this._speechIcon.y = -NAME_FONT_SIZE - 6 + Math.round(Math.sin(Graphics.frameCount / 12) * 2);
        }

        /** Where the name tag sits when nothing is in its way: just over the NPC's head. */
        nameTagHome() {
            return { x: this.x, y: this.y - this.patternHeight() + NAME_GAP + NAME_FONT_SIZE };
        }

        /** The name tag's size on screen, without the room its outline takes, and with its speech icon if shown. */
        nameTagSize() {
            const bitmap = this._nameTag.bitmap;
            const icon = this._character.isSpeaking() ? ICON_HEIGHT : 0;
            return bitmap ? { width: bitmap.width - 8, height: NAME_FONT_SIZE + 4 + icon } : { width: 0, height: 0 };
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
            this._nameTag.removeChild(this._speechIcon); // its picture is shared, and stays
            this._speechIcon.destroy();
            this._nameTag.destroy();
            super.destroy(options);
        }
    }

    ChatSaga.sprites = { Sprite_TownNpc };
})();
