//=============================================================================
// ChatSaga/Talk.js: Talking to an NPC.
//=============================================================================

// The OK button, or a click on the tile in front, talks to the NPC there: its dialogue in the message window, with
// Recruit (while the party has room), View stats and Close beside it. The message window takes \IT[1] and \IT[0] for italics on and off.

(() => {
    "use strict";

    const { FACE_PREFIX } = ChatSaga.identity;
    const { npcAt } = ChatSaga.town;

    const MESSAGE_LINES = 4; // lines of dialogue the message window shows at once

    let talkingTo = null; // the NPC whose dialogue the message window shows

    /** Whether the player is talking to this NPC now. */
    const isTalkingTo = npc => npc === talkingTo && $gameMessage.isBusy();

    /** How dialogue reads: its text, and whether it is in italics (written *like this*). */
    const dialogueLook = dialogue => {
        const italic = /^\*[^*]+\*$/.test(dialogue);
        return { text: italic ? dialogue.slice(1, -1) : dialogue, italic };
    };

    /** The width the message window has for a line of text beside a face. */
    const messageLineWidth = () =>
        Graphics.boxWidth - $gameSystem.windowPadding() * 2 - 16 - ImageManager.faceWidth - 20;

    /** The lines that fit the message window at once, the last cut with "..." when there is more. */
    const fitLines = lines => {
        if (lines.length <= MESSAGE_LINES) {
            return lines;
        }
        const shown = lines.slice(0, MESSAGE_LINES);
        shown[MESSAGE_LINES - 1] = shown[MESSAGE_LINES - 1].replace(/\s*\S{0,3}$/, "") + "...";
        return shown;
    };

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

    /** Opens the message window with what the NPC says, its avatar and its name, and the choices beside it. */
    const showDialogue = npc => {
        const look = dialogueLook(npc.npcDialogue());
        $gameMessage.setFaceImage(FACE_PREFIX + npc.npcId(), 0);
        $gameMessage.setSpeakerName(asMessageText(npc.npcName()));
        const lines = fitLines(wrapText(look.text, messageLineWidth(), look.italic));
        lines.forEach((line, i) => {
            // \> shows the text at once, so the choices come up with it
            $gameMessage.add((i === 0 ? "\\>" : "") + (look.italic ? "\\IT[1]" : "") + asMessageText(line));
        });
        offerChoices(npc);
    };

    /**
     * The choices beside what the NPC says: Recruit, while the party has room, takes it into the party; View stats
     * opens its status screen; Close, or cancelling, closes.
     */
    const offerChoices = npc => {
        const choices = [...(ChatSaga.party.canRecruit() ? ["Recruit"] : []), "View stats", "Close"];
        $gameMessage.setChoices(choices, 0, choices.length - 1);
        $gameMessage.setChoiceCallback(choice => {
            if (choices[choice] === "Recruit") {
                ChatSaga.party.recruit(npc);
            } else if (choices[choice] === "View stats") {
                ChatSaga.status.viewStats(npc);
            }
        });
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

    // Italics in the message window: \IT[1] on, \IT[0] off.
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

    ChatSaga.talk = { isTalkingTo };
})();
