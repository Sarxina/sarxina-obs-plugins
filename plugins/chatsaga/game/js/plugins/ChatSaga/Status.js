//=============================================================================
// ChatSaga/Status.js: The status panel of an NPC.
//=============================================================================

// View stats opens a panel on the right half of the screen, over the town as it goes on: the NPC's avatar, name,
// class and level, HP and MP, and its stats. OK, cancel or a click closes it. A chatter who asks in chat
// ("!chatsaga status") gets the same panel for a few seconds while the player plays on, one chatter after another. A
// party member's panel shows them as they are in the party: their level, HP and EXP so far.

(() => {
    "use strict";

    const { FACE_PREFIX, lookFor, startingEquips } = ChatSaga.identity;

    const PANEL_WIDTH = 840;
    const ROW = 48; // the height of one line of the panel
    const NAME_SIZE = 44;
    const TEXT_SIZE = 30;
    const FACE_SIZE = 144;
    const GAUGE_HEIGHT = 14;
    const PANEL_COLOR = "#1c1a2e";
    const BORDER_COLOR = "#e8e4f5";
    const STATS = [2, 3, 4, 5, 6, 7]; // Attack, Defense, M.Attack, M.Defense, Agility, Luck, as the database numbers them
    const ASKED_FRAMES = 360; // how long a panel a chatter asked for stays up

    let statusActorId = 0; // the database slot NPCs are shown in; 0 until it is made

    /** The actor whose stats the panel shows: the party member's own, or one made for the NPC. */
    const actorFor = npc => ChatSaga.party.actorOf(npc.npcId()) || newActorFor(npc);

    /** An actor with the NPC's name, class, look and avatar, at level 1 with its class's starting weapon. */
    const newActorFor = npc => {
        if (!statusActorId) {
            statusActorId = $dataActors.length;
            $dataActors.push(null);
        }
        const look = lookFor(npc.npcId());
        $dataActors[statusActorId] = {
            id: statusActorId, name: npc.npcName(), nickname: "", classId: npc.classId(), initialLevel: 1,
            maxLevel: 99, characterName: look.characterName, characterIndex: look.characterIndex,
            faceName: FACE_PREFIX + npc.npcId(), faceIndex: 0, battlerName: "", traits: [],
            equips: startingEquips(npc.classId()), profile: "", note: ""
        };
        return new Game_Actor(statusActorId);
    };

    /** The panel's height: the face and its lines, HP and MP, a gap, and the stats in two columns. */
    const panelHeight = () =>
        FACE_SIZE + 24 + ROW * 2 + 16 + ROW * Math.ceil(STATS.length / 2) + $gameSystem.windowPadding() * 2;

    /** One NPC's status, opaque, on the right half of the screen. */
    class Window_TownNpcStatus extends Window_Base {
        initialize() {
            const height = panelHeight();
            const x = Graphics.boxWidth / 2 + (Graphics.boxWidth / 2 - PANEL_WIDTH) / 2;
            super.initialize(new Rectangle(x, (Graphics.boxHeight - height) / 2, PANEL_WIDTH, height));
            this.createBackground();
            this.openness = 0;
            this._npc = null;
            this._actor = null;
            this._openedAt = 0; // the frame it opened on, whose OK press was the one that opened it
            this._closedAt = -1; // the frame it closed on, whose cancel press must not also open the menu
            this._askedFrames = 0; // while above 0, it shows a chatter who asked, and closes by itself
            this._asked = []; // NPCs who asked to see their status, waiting their turn
        }

        /** A solid panel in place of the window skin, which lets the map show through. */
        createBackground() {
            this.opacity = 0;
            const bitmap = new Bitmap(this.width, this.height);
            const context = bitmap.context;
            context.fillStyle = PANEL_COLOR;
            context.strokeStyle = BORDER_COLOR;
            context.lineWidth = 3;
            context.beginPath();
            context.roundRect(2, 2, this.width - 4, this.height - 4, 12);
            context.fill();
            context.stroke();
            bitmap.baseTexture.update();
            this._panel = this.addChildToBack(new Sprite(bitmap));
        }

        /** Whether the panel shows this NPC now. */
        isShowing(npc) {
            return this.isOpen() && this._npc === npc;
        }

        /** Whether the panel holds the screen: open, opening, or closing on this frame. A panel a chatter asked for
         * never does: the player plays on. */
        isHolding() {
            return (this.openness > 0 && this._askedFrames === 0) || this._closedAt === Graphics.frameCount;
        }

        /** Lines up the NPC's status to show for a few seconds, after any others waiting. */
        showAsked(npc) {
            if (!this._asked.includes(npc) && !(this.isOpen() && this._npc === npc)) {
                this._asked.push(npc);
            }
        }

        show(npc) {
            this._askedFrames = 0;
            this._npc = npc;
            this._actor = actorFor(npc);
            this._openedAt = Graphics.frameCount;
            this.refresh();
            this.open();
        }

        update() {
            super.update();
            this.updatePanel();
            this.updateAsked();
            if (this.isOpen() && Graphics.frameCount > this._openedAt) {
                this.updateRefresh();
                if (this._askedFrames === 0) {
                    this.updateClosing();
                }
            }
        }

        /** Opens the next asked-for panel once the panel is free, and closes it when its time is up. */
        updateAsked() {
            if (this._askedFrames > 0) {
                if (--this._askedFrames === 0) {
                    this.close();
                }
                return;
            }
            if (this.openness === 0 && this._asked.length > 0) {
                this.show(this._asked.shift());
                this._askedFrames = ASKED_FRAMES;
            }
        }

        /** Opens and closes the panel with the window, folding to and from its middle line. */
        updatePanel() {
            const open = this.openness / 255;
            this._panel.scale.y = open;
            this._panel.y = (this.height / 2) * (1 - open);
        }

        /** Redraws now and then, so an avatar that loads while the panel is open shows up. */
        updateRefresh() {
            if ((Graphics.frameCount - this._openedAt) % 30 === 0) {
                this.refresh();
            }
        }

        /** Closes on OK, cancel or a click anywhere. */
        updateClosing() {
            if (Input.isTriggered("ok") || Input.isTriggered("cancel") || TouchInput.isTriggered() ||
                TouchInput.isCancelled()) {
                SoundManager.playCancel();
                this._closedAt = Graphics.frameCount;
                this.close();
            }
        }

        refresh() {
            this.contents.clear();
            this.drawHeader();
            this.drawGauges(FACE_SIZE + 24);
            this.drawStats(FACE_SIZE + 24 + ROW * 2 + 16);
        }

        /** The avatar, with the name, class and level beside it. */
        drawHeader() {
            const actor = this._actor;
            this.drawFace(actor.faceName(), actor.faceIndex(), 0, 0, FACE_SIZE, FACE_SIZE);
            const x = FACE_SIZE + 24;
            const width = this.innerWidth - x;
            this.contents.fontSize = NAME_SIZE;
            this.changeTextColor(ColorManager.normalColor());
            this.drawText(actor.name(), x, 4, width);
            this.contents.fontSize = TEXT_SIZE;
            this.changeTextColor(ColorManager.systemColor());
            this.drawText(actor.currentClass().name, x, 64, width);
            this.drawText(TextManager.levelA + " " + actor.level, x, 64 + ROW, width);
            this.resetTextColor();
        }

        /** HP and MP, each a label, a bar and its numbers. */
        drawGauges(y) {
            const actor = this._actor;
            this.drawGauge(TextManager.hpA, actor.hp, actor.mhp, ColorManager.hpGaugeColor1(),
                ColorManager.hpGaugeColor2(), y);
            this.drawGauge(TextManager.mpA, actor.mp, actor.mmp, ColorManager.mpGaugeColor1(),
                ColorManager.mpGaugeColor2(), y + ROW);
        }

        drawGauge(label, value, max, color1, color2, y) {
            const labelWidth = 72;
            const numberWidth = 180;
            const barX = labelWidth;
            const barWidth = this.innerWidth - labelWidth - numberWidth - 16;
            const barY = y + (ROW - GAUGE_HEIGHT) / 2;
            this.contents.fontSize = TEXT_SIZE;
            this.changeTextColor(ColorManager.systemColor());
            this.drawText(label, 0, y, labelWidth);
            this.contents.fillRect(barX, barY, barWidth, GAUGE_HEIGHT, ColorManager.gaugeBackColor());
            this.contents.gradientFillRect(barX, barY, Math.round(barWidth * (max ? value / max : 0)), GAUGE_HEIGHT,
                color1, color2);
            this.resetTextColor();
            this.drawText(value + " / " + max, this.innerWidth - numberWidth, y, numberWidth, "right");
        }

        /** The six stats, in two columns. */
        drawStats(y) {
            this.contents.fontSize = TEXT_SIZE;
            const columnWidth = (this.innerWidth - 48) / 2;
            STATS.forEach((paramId, i) => {
                const x = (i % 2) * (columnWidth + 48);
                const rowY = y + Math.floor(i / 2) * ROW;
                this.changeTextColor(ColorManager.systemColor());
                this.drawText(TextManager.param(paramId), x, rowY, columnWidth);
                this.resetTextColor();
                this.drawText(this._actor.param(paramId), x, rowY, columnWidth, "right");
            });
        }
    }

    /** The panel on the map shown now, if there is one. */
    const panel = () => {
        const scene = SceneManager._scene;
        return scene instanceof Scene_Map ? scene._townNpcStatusWindow : null;
    };

    /** Whether the panel holds the screen, so the player, the map and the menu wait. */
    const isPanelHolding = () => !!panel() && panel().isHolding();

    const _Scene_Map_createAllWindows = Scene_Map.prototype.createAllWindows;
    Scene_Map.prototype.createAllWindows = function() {
        _Scene_Map_createAllWindows.call(this);
        this._townNpcStatusWindow = new Window_TownNpcStatus();
        this.addWindow(this._townNpcStatusWindow);
    };

    // While the panel is up, the player stays put, clicks on the map go nowhere, and cancel closes it, not the menu.
    const _Game_Player_canMove = Game_Player.prototype.canMove;
    Game_Player.prototype.canMove = function() {
        return !isPanelHolding() && _Game_Player_canMove.call(this);
    };

    const _Scene_Map_isMapTouchOk = Scene_Map.prototype.isMapTouchOk;
    Scene_Map.prototype.isMapTouchOk = function() {
        return !isPanelHolding() && _Scene_Map_isMapTouchOk.call(this);
    };

    const _Scene_Map_isMenuEnabled = Scene_Map.prototype.isMenuEnabled;
    Scene_Map.prototype.isMenuEnabled = function() {
        return !isPanelHolding() && _Scene_Map_isMenuEnabled.call(this);
    };

    /** Opens the status panel for the NPC. */
    const viewStats = npc => {
        if (panel()) {
            panel().show(npc);
        }
    };

    /** Whether the status panel shows this NPC now. */
    const isShowing = npc => !!panel() && panel().isShowing(npc);

    /** Shows the status of the NPC with this id for a few seconds, as its chatter asked in chat. */
    const showAsked = id => {
        const npc = ChatSaga.town.npcById(id);
        if (npc && panel()) {
            panel().showAsked(npc);
        }
    };

    ChatSaga.status = { viewStats, isShowing, showAsked };
})();
