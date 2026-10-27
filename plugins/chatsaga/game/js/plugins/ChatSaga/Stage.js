//=============================================================================
// ChatSaga/Stage.js: The game as it runs on stream.
//=============================================================================

// The game opens straight into the town and follows the feed from then on, fills the window or OBS source it is shown
// in, has no menu button in the corner (Escape still opens the menu), and keeps going while another window has the
// focus.

(() => {
    "use strict";

    const { followFeed } = ChatSaga.feed;

    // The town keeps going while another window has the focus, as it does on stream.
    SceneManager.isGameActive = function() {
        return true;
    };

    // The town fills the browser window or OBS source it is shown in, whatever its size.
    Graphics._defaultStretchMode = function() {
        return true;
    };

    // No menu button in the corner: the town is on stream (Escape still opens the menu).
    Scene_Map.prototype.createButtons = function() {};

    // The game opens straight into the town, and follows the feed from then on: an NPC needs the database (its class).
    // There is no saving: the town lives on stream, and the party is kept in the browser.
    Scene_Boot.prototype.startNormalGame = function() {
        this.checkPlayerLocation();
        DataManager.setupNewGame();
        $gameSystem.disableSave();
        followFeed();
        SceneManager.goto(Scene_Map);
    };
})();
