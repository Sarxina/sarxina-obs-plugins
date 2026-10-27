//=============================================================================
// ChatSaga/Npc.js: An NPC on the map.
//=============================================================================

// Game_TownNpc: one NPC, with its name, dialogue, class and avatar, that strolls about the town and rests in between.

(() => {
    "use strict";

    const { randomBetween, npcName, npcDialogue, spokeTime, lookFor, npcClass } = ChatSaga.identity;
    const { loadAvatar } = ChatSaga.avatars;

    const WALK_SPEED = 3; // RPG Maker's move speed: 3 is a stroll
    const STROLL_STEPS = [1, 4]; // steps in one stroll, fewest and most
    const REST_FRAMES = [120, 480]; // the rest between strolls, shortest and longest
    const SPEAKING_MS = 15000; // how long the speech icon shows after an NPC speaks

    /** One NPC on the map, strolling about the town, with a name and something to say. */
    class Game_TownNpc extends Game_Character {
        initialize(id, name, dialogue, spokeAt, classId, avatar) {
            super.initialize();
            this._npcId = id;
            this._npcName = name;
            this._npcDialogue = dialogue;
            this._spokeAt = spokeAt;
            this._classId = npcClass(classId, id);
            this._avatar = avatar;
            loadAvatar(id, avatar);
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

        spokeAt() {
            return this._spokeAt;
        }

        classId() {
            return this._classId;
        }

        avatar() {
            return this._avatar;
        }

        /** Whether it spoke lately enough to show the speech icon. */
        isSpeaking() {
            return Date.now() - this._spokeAt < SPEAKING_MS;
        }

        /** Takes what is given of a new name, dialogue, time it last spoke, class and avatar. */
        change({ name, dialogue, spokeAt, classId, avatar }) {
            if (name !== undefined) {
                this._npcName = npcName(name, this._npcId);
            }
            if (dialogue !== undefined) {
                this._npcDialogue = npcDialogue(dialogue);
            }
            if (spokeAt !== undefined) {
                this._spokeAt = spokeTime(spokeAt);
            }
            if (classId !== undefined) {
                this._classId = npcClass(classId, this._npcId);
            }
            if (avatar !== undefined && avatar !== this._avatar) {
                this._avatar = avatar;
                loadAvatar(this._npcId, avatar);
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

        /** Walks a few steps now and then, and rests in between; never while the player talks to it or views it. */
        updateStroll() {
            if (this.isMoving() || ChatSaga.talk.isTalkingTo(this) || ChatSaga.status.isShowing(this)) {
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
            const direction = ChatSaga.stroll.chooseStep(this);
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

    ChatSaga.npc = { Game_TownNpc };
})();
