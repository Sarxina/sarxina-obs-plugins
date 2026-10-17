/*
 * How a Sarxina OBS plugin talks to the Sarxina OBS Plugin Manager.
 *
 * Plugins and the manager are separate OBS plugins, released separately. Both build against this header and neither
 * links the other: the calls go through OBS's global procedure handler, which is there whether or not the manager is
 * installed. A plugin hands itself to the manager from obs_module_post_load, when every plugin has loaded. If the
 * manager is not installed, sarxina_obs_plugin_manager_register() returns false and the plugin switches itself on and
 * runs on its own, as if the manager did not exist.
 *
 * The manager owns what plugins share: which ones are installed, their settings (drawn from what the plugin
 * says about them here, so the manager needs to know nothing about any plugin), one chat connection that hands plugins
 * the chat they asked for, and one connection to VTube Studio. BUILDING.md has the steps for a new plugin, and
 * examples/example-plugin is the smallest one.
 *
 * Everything happens on OBS's UI thread. Nothing here may be called after OBS_FRONTEND_EVENT_EXIT.
 */
#pragma once

#include <callback/proc.h>
#include <obs.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SARXINA_OBS_PLUGIN_MANAGER_API 1

/* ---- chat -------------------------------------------------------------------------------------------------------- */

/* Who may run a chat command, lowest first. A chatter counts as the highest one that applies to them. */
enum sarxina_role {
	SARXINA_ROLE_EVERYONE,
	SARXINA_ROLE_SUBSCRIBER,
	SARXINA_ROLE_VIP,
	SARXINA_ROLE_MODERATOR,
	SARXINA_ROLE_BROADCASTER,
};

struct sarxina_badge {
	const char *name;    /* "subscriber" */
	const char *version; /* "12" */
};

struct sarxina_emote {
	const char *id;
	size_t begin, end; /* where it is in the text, in bytes; end is one past its last byte */
};

/* One chat message, the same for every platform. Every string is UTF-8 without control characters, and the text is at
 * most 500 characters; the manager has dropped anything else. It all lives only for the call it is passed to. */
struct sarxina_chat_message {
	const char *platform; /* "twitch" */
	const char *id;       /* the platform's id for the message */
	const char *user_id;
	const char *user_name; /* the account name, as typed in an @mention */
	const char *display_name;
	enum sarxina_role role;
	const struct sarxina_badge *badges;
	size_t badge_count;
	const char *text;
	const struct sarxina_emote *emotes;
	size_t emote_count;
	int64_t time_ms; /* when the platform got it, in milliseconds since 1970 UTC */
	/* The id of the channel point reward the message was sent with, or "". Only a reward that asks the viewer for
	 * text comes through chat, as the text they typed; Twitch gives its id, not its name. */
	const char *reward_id;
};

/* A chat command a plugin answers to, typed in chat as "!name" or "!name what follows". The manager matches it in any
 * case and makes every check; the plugin only hears the commands that may run. */
struct sarxina_chat_command {
	const char *name;           /* without the "!": 1 to 25 lower-case letters, digits and _ */
	const char *const *aliases; /* other names for the same command, ending with NULL; or NULL */
	enum sarxina_role who;      /* the lowest role that may run it */
	uint32_t cooldown_ms;       /* after it runs, it is ignored for this long, whoever sends it */
	/* `args` is what follows the name, trimmed. */
	void (*run)(void *data, const char *args, const struct sarxina_chat_message *message);
};

/* ---- the plugin -------------------------------------------------------------------------------------------------- */

struct sarxina_plugin_info {
	uint32_t api;            /* SARXINA_OBS_PLUGIN_MANAGER_API */
	const char *id;          /* the module's name: "slinglive" */
	const char *name;        /* as shown: "SlingLive" */
	const char *description; /* one sentence */
	void *data;              /* handed back to every callback */

	/* Off means the plugin acts as if it were not installed. Called with true while the plugin registers, with false
	 * if the streamer uninstalls it while OBS runs (OBS no longer loads it once it restarts), and with true again if
	 * they take that back before the restart. */
	void (*set_enabled)(void *data, bool enabled);

	/* Optional: the plugin's settings, described with OBS's own properties and kept by the plugin. The manager draws
	 * them, fills in the current values with get_settings, and hands every change to update. */
	obs_properties_t *(*get_properties)(void *data);
	void (*get_settings)(void *data, obs_data_t *settings);
	void (*update)(void *data, obs_data_t *settings);

	/* Optional: the chat commands it answers to. */
	const struct sarxina_chat_command *commands;
	size_t command_count;

	/* Optional: every chat message that passed the manager's checks, for a plugin that uses chat its own way. */
	void (*chat)(void *data, const struct sarxina_chat_message *message);
};

/* Hands the plugin to the manager. Call it from obs_module_post_load; `info` has to stay valid until OBS exits. True
 * when the manager took it, by which time it has called set_enabled. False when the manager is not installed (or
 * refused the plugin, which it explains in the log): the plugin should switch itself on. */
static inline bool sarxina_obs_plugin_manager_register(const struct sarxina_plugin_info *info)
{
	proc_handler_t *procs = obs_get_proc_handler();
	uint8_t stack[128];
	calldata_t call;
	calldata_init_fixed(&call, stack, sizeof(stack));
	calldata_set_ptr(&call, "plugin", (void *)info);
	return procs && proc_handler_call(procs, "sarxina_obs_plugin_manager_register", &call) &&
	       calldata_bool(&call, "accepted");
}

/* The plugin changed its own settings (from its dock, say): the manager shows them again. */
static inline void sarxina_obs_plugin_manager_settings_changed(const char *id)
{
	proc_handler_t *procs = obs_get_proc_handler();
	uint8_t stack[128];
	calldata_t call;
	calldata_init_fixed(&call, stack, sizeof(stack));
	calldata_set_string(&call, "id", id);
	if (procs)
		proc_handler_call(procs, "sarxina_obs_plugin_manager_settings_changed", &call);
}

/* ---- VTube Studio ------------------------------------------------------------------------------------------------ */

/* A request to VTube Studio's public API, sent over the manager's one connection. `type` is the API's messageType
 * ("HotkeysInCurrentModelRequest", "HotkeyTriggerRequest", ...) and `data` its data object as JSON, or NULL. `done` is
 * called once, later, with the answer's data object as JSON. `ok` is false when VTube Studio answered with an error or
 * the manager is not connected to it; the JSON then has a "message". */
struct sarxina_vts_request {
	const char *type;
	const char *data;
	void (*done)(void *param, bool ok, const char *data);
	void *param;
};

/* Sends a request. False when the manager is not installed, and then `done` is never called. */
static inline bool sarxina_obs_plugin_manager_vts_request(const struct sarxina_vts_request *request)
{
	proc_handler_t *procs = obs_get_proc_handler();
	uint8_t stack[128];
	calldata_t call;
	calldata_init_fixed(&call, stack, sizeof(stack));
	calldata_set_ptr(&call, "request", (void *)request);
	return procs && proc_handler_call(procs, "sarxina_obs_plugin_manager_vts_request", &call);
}

#ifdef __cplusplus
}
#endif
