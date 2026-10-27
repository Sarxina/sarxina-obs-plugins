//=============================================================================
// ChatSaga/Identity.js: Who an NPC is.
//=============================================================================

// An NPC's id, name, dialogue, look and class, worked out from what it is given.

(() => {
    "use strict";

    const LONGEST_ID = 32;
    const LONGEST_NAME = 32;
    const LONGEST_DIALOGUE = 500; // as long as a chat message gets

    // The Time Fantasy character sheets NPCs are dressed from: 8 characters each.
    const LOOKS = [
        "npc1", "npc2", "npc3", "npc4",
        "chara1", "chara2", "chara3", "chara4", "chara5",
        "children_mv", "military1", "military2", "military3", "bonus1"
    ];

    /** A whole number from the first to the second of the pair, both included. */
    const randomBetween = ([low, high]) => low + Math.randomInt(high - low + 1);

    /** Text as it is shown: one line, with nothing that hides or turns text around. */
    const plainText = text =>
        String(text ?? "")
            .replace(/[\u0000-\u001f\u007f-\u009f​-‏‪-‮⁦-⁩﻿]/g, "")
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

    /** When an NPC last spoke, in milliseconds since 1970; 0 for not lately. */
    const spokeTime = given => Math.max(0, Number(given) || 0);

    /** A number that is always the same for the same id, and spread evenly over ids. */
    const idHash = id => {
        let hash = 2166136261;
        for (const c of id) {
            hash = Math.imul(hash ^ c.codePointAt(0), 16777619) >>> 0;
        }
        return hash;
    };

    /** The character sheet and index an NPC with this id wears. */
    const lookFor = id => {
        const hash = idHash(id);
        return { characterName: LOOKS[hash % LOOKS.length], characterIndex: (hash >>> 8) % 8 };
    };

    /** The NPC's class: the one given if the database has it, else one picked from its id. */
    const npcClass = (given, id) => {
        const classId = Number(given) || 0;
        if ($dataClasses[classId]) {
            return classId;
        }
        const classes = $dataClasses.filter(Boolean);
        return classes[(idHash(id) >>> 16) % classes.length].id;
    };

    ChatSaga.identity = {
        LONGEST_ID, FACE_PREFIX: "chatsaga:", randomBetween, npcId, npcName, npcDialogue, spokeTime, idHash, lookFor,
        npcClass
    };
})();
