//=============================================================================
// ChatSaga/Party.js: The player's party of chatters recruited from town.
//=============================================================================

// Talking to an NPC offers Recruit while the party has room. The party is the chatters the player recruits, up to a
// full battle party of four; the player, the streamer, leads them but isn't one of them. A recruit leaves the town's
// crowd and follows the player, with their name, class, look and avatar. A member stays in the party while they are in
// the stream, lurking or not; one who has been out of it for two hours leaves, so a long dungeon never hangs on someone
// who had to go. Dismiss in the menu sends one home at once. Members who leave stroll the town again if they are still
// in the stream. The party outlasts the page reloading.

(() => {
    "use strict";

    const { FACE_PREFIX, npcId, lookFor, npcName, npcClass, spokeTime, startingEquips } = ChatSaga.identity;
    const { loadAvatar } = ChatSaga.avatars;
    const { npcById } = ChatSaga.town;

    const STREAMER_ACTOR_ID = 1; // the player: the streamer, who leads the party without being in it
    const AWAY_LIMIT_MS = 2 * 60 * 60 * 1000; // a member out of the stream this long leaves the party
    const CHECK_FRAMES = 60; // how often the party looks for members who have left the stream
    const NOTICE_FRAMES = 240; // how long a notice stays up
    const STORAGE_KEY = "ChatSaga.party";

    const members = new Map(); // id -> { id, name, classId, avatar, goneSince, actorId }, in the order they joined
    const joinings = []; // { id, name } of recruits whose "joined the party!" waits for the message window to be free
    let lastCheck = 0; // the frame the party last looked for members who have left the stream

    /** Whether this chatter is in the party. */
    const has = id => members.has(id);

    /** Whether the party has room for another recruit: it holds a full battle party. */
    const canRecruit = () => members.size < $gameParty.maxBattleMembers();

    /** A new database slot for a member's actor, with their name, class, look and avatar at level 1, holding their
     * class's starting weapon. */
    const makeActor = member => {
        const actorId = $dataActors.length;
        const look = lookFor(member.id);
        $dataActors.push({
            id: actorId, name: member.name, nickname: "", classId: member.classId, initialLevel: 1, maxLevel: 99,
            characterName: look.characterName, characterIndex: look.characterIndex,
            faceName: FACE_PREFIX + member.id, faceIndex: 0, battlerName: "", traits: [],
            equips: startingEquips(member.classId), profile: "", note: ""
        });
        return actorId;
    };

    /** Puts a chatter in the party, behind the player. goneSince is when they left the stream, or 0 while in it. */
    const join = (id, { name, classId, avatar, goneSince }) => {
        const member = { id, name: npcName(name, id), classId: npcClass(classId, id), avatar: avatar || "",
            goneSince };
        loadAvatar(id, member.avatar);
        member.actorId = makeActor(member);
        members.set(id, member);
        $gameParty.addActor(member.actorId);
        remember();
    };

    /** Recruits the NPC: it leaves the town's crowd and follows the player. */
    const recruit = npc => {
        if (!canRecruit() || has(npc.npcId())) {
            return;
        }
        join(npc.npcId(), { name: npc.npcName(), classId: npc.classId(), avatar: npc.avatar(), goneSince: 0 });
        npc.leaveMap();
        joinings.push({ id: npc.npcId(), name: npc.npcName() });
    };

    /** Takes a member out of the party; they stroll the town again if they are still in the stream. */
    const sendHome = (id, notice) => {
        const member = members.get(id);
        if (!member) {
            return;
        }
        $gameParty.removeActor(member.actorId);
        members.delete(id);
        remember();
        showNotice(notice);
    };

    /** The member whose actor this is, if any. */
    const memberOf = actor => [...members.values()].find(member => member.actorId === actor.actorId());

    /** The party member's actor, for the chatter with this id; none when they are not in the party. */
    const actorOf = id => (members.has(id) ? $gameActors.actor(members.get(id).actorId) : null);

    /**
     * A chatter changed class ("!chatsaga changeclass"): a line on screen, and a party member takes up the new class at
     * the level they reached, holding its starting weapon.
     */
    const classChanged = npc => {
        showNotice(npc.npcName() + " is now a " + $dataClasses[npc.classId()].name);
        const member = members.get(npc.npcId());
        if (!member) {
            return;
        }
        member.classId = npc.classId();
        const actor = $gameActors.actor(member.actorId);
        actor.changeClass(member.classId, true);
        actor.releaseUnequippableItems(true);
        const [weaponId] = startingEquips(member.classId);
        if (weaponId && actor.weapons().length === 0) {
            actor.forceChangeEquip(0, $dataWeapons[weaponId]);
        }
        remember();
    };

    /**
     * Notes who is in the stream: a member in town is (and their name is kept up to date from there); one who isn't
     * has been gone since the first time they were missed.
     */
    const keepUp = () => {
        const now = Date.now();
        for (const member of members.values()) {
            const npc = npcById(member.id);
            if (npc) {
                member.goneSince = 0;
                member.name = npc.npcName();
                $gameActors.actor(member.actorId).setName(member.name);
            } else if (!member.goneSince) {
                member.goneSince = now;
            }
        }
    };

    /** Whether a member has been out of the stream for too long to stay in the party. */
    const isLongGone = (member, now) => member.goneSince > 0 && now - member.goneSince >= AWAY_LIMIT_MS;

    /** Takes out of the party every member who has been out of the stream for two hours. */
    const sendLongGoneHome = () => {
        keepUp();
        const now = Date.now();
        for (const member of [...members.values()]) {
            if (isLongGone(member, now)) {
                sendHome(member.id, member.name + " left the party after two hours away");
            }
        }
        remember();
    };

    /** Keeps the party in the browser, so a reload brings back everyone who hasn't been away too long. */
    const remember = () => {
        const kept = [...members.values()].map(({ id, name, classId, avatar, goneSince }) =>
            ({ id, name, classId, avatar, goneSince }));
        try {
            localStorage.setItem(STORAGE_KEY, JSON.stringify(kept));
        } catch (e) {
            // a browser that keeps nothing just starts each page with an empty party
        }
    };

    /** The members the browser kept, as they were. */
    const remembered = () => {
        try {
            const kept = JSON.parse(localStorage.getItem(STORAGE_KEY) || "[]");
            return Array.isArray(kept) ? kept : [];
        } catch (e) {
            return [];
        }
    };

    /** Brings back the members kept before the page reloaded, but not those who have been away too long since. */
    const bringBack = () => {
        const now = Date.now();
        for (const kept of remembered()) {
            const id = npcId(kept.id);
            const member = { ...kept, goneSince: spokeTime(kept.goneSince) };
            if (id && !has(id) && canRecruit() && !isLongGone(member, now)) {
                join(id, member);
            }
        }
        remember();
    };

    /** Short lines at the top of the screen, one after another, for the party's goings while the player plays on. */
    class Window_PartyNotice extends Window_Base {
        initialize() {
            const height = this.fittingHeight(1);
            super.initialize(new Rectangle(0, 24, Graphics.boxWidth, height));
            this.opacity = 0;
            this.contentsOpacity = 0;
            this._waiting = []; // lines still to show
            this._framesLeft = 0; // of the line shown now
        }

        show(text) {
            this._waiting.push(text);
        }

        /** Draws the next waiting line, centred, as plain text (a name's backslashes are not escape codes). */
        showNext() {
            this.contents.clear();
            this.drawText(this._waiting.shift(), 0, 0, this.innerWidth, "center");
            this._framesLeft = NOTICE_FRAMES;
        }

        update() {
            super.update();
            if (this._framesLeft > 0) {
                this._framesLeft--;
            } else if (this._waiting.length > 0 && this.contentsOpacity === 0) {
                this.showNext();
            }
            const target = this._framesLeft > 30 ? 255 : (this._framesLeft / 30) * 255;
            this.contentsOpacity += Math.sign(target - this.contentsOpacity) * Math.min(24,
                Math.abs(target - this.contentsOpacity));
        }
    }

    /** Shows a notice on the map, if the map is up. */
    const showNotice = text => {
        const scene = SceneManager._scene;
        if (scene instanceof Scene_Map && scene._partyNoticeWindow) {
            scene._partyNoticeWindow.show(text);
        }
    };

    /** Shows the next "joined the party!", with the recruit's avatar, once the talk that recruited them has closed. */
    const announceJoining = () => {
        if (joinings.length > 0 && !$gameMessage.isBusy()) {
            const { id, name } = joinings.shift();
            $gameMessage.setFaceImage(FACE_PREFIX + id, 0);
            $gameMessage.add(name.replace(/\\/g, "\\\\") + " joined the party!"); // a backslash in a name stays one
        }
    };

    const _Scene_Map_createAllWindows = Scene_Map.prototype.createAllWindows;
    Scene_Map.prototype.createAllWindows = function() {
        _Scene_Map_createAllWindows.call(this);
        this._partyNoticeWindow = new Window_PartyNotice();
        this.addWindow(this._partyNoticeWindow);
    };

    const _Game_Map_update = Game_Map.prototype.update;
    Game_Map.prototype.update = function(sceneActive) {
        _Game_Map_update.call(this, sceneActive);
        announceJoining();
        if (Graphics.frameCount - lastCheck >= CHECK_FRAMES) {
            lastCheck = Graphics.frameCount;
            sendLongGoneHome();
        }
    };

    // A new game starts with the streamer alone, then the members kept from before walking behind.
    const _DataManager_setupNewGame = DataManager.setupNewGame;
    DataManager.setupNewGame = function() {
        _DataManager_setupNewGame.call(this);
        $gameParty.removeActor(STREAMER_ACTOR_ID);
        $gamePlayer.showFollowers();
        bringBack();
    };

    // The player always looks like the streamer, whoever is in the party.
    Game_Player.prototype.refresh = function() {
        const streamer = $gameActors.actor(STREAMER_ACTOR_ID);
        this.setImage(streamer.characterName(), streamer.characterIndex());
        this._followers.refresh();
    };

    // Every party member follows the player, the first one included: the streamer leads without being in the party.
    Game_Followers.prototype.setup = function() {
        this._data = [];
        for (let i = 0; i < $gameParty.maxBattleMembers(); i++) {
            this._data.push(new Game_Follower(i));
        }
    };

    // An empty party is the streamer walking about alone, not a game over.
    const _Game_Party_isAllDead = Game_Party.prototype.isAllDead;
    Game_Party.prototype.isAllDead = function() {
        return this.size() > 0 && _Game_Party_isAllDead.call(this);
    };

    // Followers carry their member's name and speech icon, the way NPCs do.
    Game_Follower.prototype.npcName = function() {
        const member = this.actor() && memberOf(this.actor());
        return member ? member.name : "";
    };

    Game_Follower.prototype.isSpeaking = function() {
        const member = this.actor() && memberOf(this.actor());
        const npc = member && npcById(member.id);
        return !!npc && npc.isSpeaking();
    };

    // Dismiss in the menu sends the chosen member home.
    const _Window_MenuCommand_addOriginalCommands = Window_MenuCommand.prototype.addOriginalCommands;
    Window_MenuCommand.prototype.addOriginalCommands = function() {
        _Window_MenuCommand_addOriginalCommands.call(this);
        this.addCommand("Dismiss", "dismiss", members.size > 0);
    };

    const _Scene_Menu_createCommandWindow = Scene_Menu.prototype.createCommandWindow;
    Scene_Menu.prototype.createCommandWindow = function() {
        _Scene_Menu_createCommandWindow.call(this);
        this._commandWindow.setHandler("dismiss", this.commandPersonal.bind(this));
    };

    // The member picked in the menu is noted before the pick is acted on (RPG Maker notes it after), so Dismiss knows
    // who to send home and nothing looks for them once they are gone.
    Window_MenuStatus.prototype.processOk = function() {
        $gameParty.setMenuActor(this.actor(this.index()));
        Window_StatusBase.prototype.processOk.call(this);
    };

    const _Scene_Menu_onPersonalOk = Scene_Menu.prototype.onPersonalOk;
    Scene_Menu.prototype.onPersonalOk = function() {
        if (this._commandWindow.currentSymbol() !== "dismiss") {
            _Scene_Menu_onPersonalOk.call(this);
            return;
        }
        const member = memberOf($gameParty.menuActor());
        if (!member) {
            SoundManager.playBuzzer();
            this._statusWindow.activate();
            return;
        }
        sendHome(member.id, member.name + " left the party");
        this._statusWindow.refresh();
        this._commandWindow.refresh();
        this.onPersonalCancel();
    };

    ChatSaga.party = { has, canRecruit, recruit, actorOf, classChanged };
})();
