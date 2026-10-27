//=============================================================================
// ChatSaga/Avatars.js: Each NPC's avatar, as a face.
//=============================================================================

// Faces named chatsaga:<id> are that NPC's avatar: its picture once loaded, and its initial on a colour of its own
// until then or when it has none.

(() => {
    "use strict";

    const { FACE_PREFIX, idHash } = ChatSaga.identity;

    const avatars = new Map(); // NPC id -> Bitmap

    /** The NPC's avatar as a face: its picture once loaded, the default until then and when it has none. */
    const avatarFor = id => {
        if (!avatars.has(id)) {
            avatars.set(id, defaultAvatar(id));
        }
        return avatars.get(id);
    };

    /** A face for someone without a picture: their initial on a colour picked from their id. */
    const defaultAvatar = id => {
        const size = ImageManager.faceWidth;
        const bitmap = new Bitmap(size, ImageManager.faceHeight);
        const hue = idHash(id) % 360;
        bitmap.fillRect(0, 0, size, bitmap.height, "hsl(" + hue + ", 45%, 42%)");
        bitmap.fontFace = $gameSystem.mainFontFace();
        bitmap.fontSize = Math.round(size * 0.55);
        bitmap.outlineWidth = 6;
        bitmap.drawText(([...id][0] || "?").toUpperCase(), 0, 0, size, bitmap.height, "center");
        return bitmap;
    };

    /** Draws the picture at that address into the NPC's avatar, filling the face slot, once it has loaded. */
    const loadAvatar = (id, address) => {
        if (!address) {
            return;
        }
        const image = new Image();
        image.crossOrigin = "anonymous";
        image.onload = () => paintAvatar(avatarFor(id), image);
        image.src = address;
    };

    /** Paints the image over the bitmap, cropped to a square in its middle. */
    const paintAvatar = (bitmap, image) => {
        const side = Math.min(image.width, image.height);
        bitmap.clear();
        bitmap.context.drawImage(image, (image.width - side) / 2, (image.height - side) / 2, side, side,
            0, 0, bitmap.width, bitmap.height);
        bitmap.baseTexture.update();
    };

    const _ImageManager_loadFace = ImageManager.loadFace;
    ImageManager.loadFace = function(filename) {
        return filename.startsWith(FACE_PREFIX) ? avatarFor(filename.slice(FACE_PREFIX.length))
            : _ImageManager_loadFace.call(this, filename);
    };

    ChatSaga.avatars = { loadAvatar };
})();
