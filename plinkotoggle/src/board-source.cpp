#include "board-source.hpp"

#include <obs-module.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace sarxina::plinkotoggle {

namespace {

// Draws a premultiplied picture at an opacity, the way OBS draws its own image source.
constexpr const char *kSpriteEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform float opacity;

sampler_state def_sampler {
	Filter   = Linear;
	AddressU = Clamp;
	AddressV = Clamp;
};

struct VertInOut {
	float4 pos : POSITION;
	float2 uv  : TEXCOORD0;
};

VertInOut VSDefault(VertInOut vert_in)
{
	VertInOut vert_out;
	vert_out.pos = mul(float4(vert_in.pos.xyz, 1.0), ViewProj);
	vert_out.uv  = vert_in.uv;
	return vert_out;
}

float4 PSDraw(VertInOut vert_in) : TARGET
{
	return image.Sample(def_sampler, vert_in.uv) * opacity;
}

technique Draw
{
	pass
	{
		vertex_shader = VSDefault(vert_in);
		pixel_shader  = PSDraw(vert_in);
	}
}
)";

struct Drop {
	QString who;
	std::optional<uint32_t> seed;
};

// A board's pictures on the graphics card: the still picture, uploaded again only when it changes, and the small
// pictures that move over it.
struct Textures {
	gs_texture_t *still = nullptr;
	uint64_t stillVersion = 0;
	gs_texture_t *head = nullptr;
	std::map<int, gs_texture_t *> buttons;    // flashing, by slot; made for one still picture
	std::map<QString, gs_texture_t *> names; // the tags of the balls on the board

	void clearButtons()
	{
		for (auto &[slot, texture] : buttons)
			gs_texture_destroy(texture);
		buttons.clear();
	}
	void destroy()
	{
		gs_texture_destroy(still);
		gs_texture_destroy(head);
		clearButtons();
		for (auto &[name, texture] : names)
			gs_texture_destroy(texture);
		names.clear();
		still = head = nullptr;
	}
};

struct Source {
	obs_source_t *source = nullptr;
	std::atomic<bool> active{false};
	// The board's size in pixels, which grows with the toggles. OBS asks for it from more than one thread.
	std::atomic<uint32_t> width{0}, height{0};

	// Handed over from the UI thread, under `lock`.
	std::mutex lock;
	bool newContent = false;
	Content content;
	std::vector<Drop> drops;

	// The graphics thread's own.
	ToggleBoard board;
	std::vector<ToggleBoard::Sprite> sprites;
	Textures textures;
};

std::mutex sourcesLock; // guards the three below
std::vector<Source *> sources;
Content shown;
void (*landingHandler)(const Landing &) = nullptr;

std::atomic<bool> closing{false};
gs_effect_t *spriteEffect = nullptr; // made on the graphics thread the first time a board is drawn

void handLanding(void *param)
{
	std::unique_ptr<Landing> landing(static_cast<Landing *>(param));
	if (!closing && landingHandler)
		landingHandler(*landing);
}

gs_texture_t *textureOf(const QImage &image)
{
	const uint8_t *bits = image.constBits();
	return gs_texture_create(static_cast<uint32_t>(image.width()), static_cast<uint32_t>(image.height()), GS_BGRA, 1,
				 &bits, 0);
}

// Draws a picture centred on a point, turned and faded.
void drawCentred(gs_texture_t *texture, QPointF centre, double rotation, double opacity)
{
	if (!texture)
		return;
	const uint32_t width = gs_texture_get_width(texture), height = gs_texture_get_height(texture);
	gs_effect_set_texture(gs_effect_get_param_by_name(spriteEffect, "image"), texture);
	gs_effect_set_float(gs_effect_get_param_by_name(spriteEffect, "opacity"), static_cast<float>(opacity));
	gs_matrix_push();
	gs_matrix_translate3f(static_cast<float>(centre.x()), static_cast<float>(centre.y()), 0);
	if (rotation != 0)
		gs_matrix_rotaa4f(0, 0, 1, static_cast<float>(rotation));
	gs_matrix_translate3f(-static_cast<float>(width) / 2, -static_cast<float>(height) / 2, 0);
	while (gs_effect_loop(spriteEffect, "Draw"))
		gs_draw_sprite(texture, 0, width, height);
	gs_matrix_pop();
}

// ---- the source, as OBS calls it ------------------------------------------------------------------------------------

const char *name(void *)
{
	return obs_module_text("SourceName");
}

void *create(obs_data_t *, obs_source_t *obsSource)
{
	auto *source = new Source();
	source->source = obsSource;
	source->width = static_cast<uint32_t>(source->board.pictureSize().width());
	source->height = static_cast<uint32_t>(source->board.pictureSize().height());
	std::lock_guard<std::mutex> hold(sourcesLock);
	source->content = shown;
	source->newContent = true;
	sources.push_back(source);
	return source;
}

void destroy(void *data)
{
	auto *source = static_cast<Source *>(data);
	{
		std::lock_guard<std::mutex> hold(sourcesLock);
		sources.erase(std::remove(sources.begin(), sources.end(), source), sources.end());
	}
	obs_enter_graphics();
	source->textures.destroy();
	obs_leave_graphics();
	delete source;
}

uint32_t width(void *data)
{
	return static_cast<Source *>(data)->width;
}

uint32_t height(void *data)
{
	return static_cast<Source *>(data)->height;
}

void activate(void *data)
{
	static_cast<Source *>(data)->active = true;
}

void deactivate(void *data)
{
	static_cast<Source *>(data)->active = false;
}

// On the graphics thread, every frame: what the UI thread handed over, then time, then where everything is.
void tick(void *data, float seconds)
{
	auto *source = static_cast<Source *>(data);
	ToggleBoard &board = source->board;
	std::vector<Drop> drops;
	bool changed = false;
	{
		std::lock_guard<std::mutex> hold(source->lock);
		if (source->newContent) {
			board.setContent(source->content);
			source->newContent = false;
			changed = true;
		}
		drops.swap(source->drops);
	}
	for (const Drop &drop : drops) {
		if (drop.seed)
			board.drop(drop.who, *drop.seed);
		else
			board.drop(drop.who);
	}

	const bool moving = board.animating() || changed || !drops.empty();
	std::vector<Landing> landed;
	board.advance(seconds, landed);
	for (const Landing &landing : landed)
		if (!closing)
			obs_queue_task(OBS_TASK_UI, handLanding, new Landing(landing), false);

	if (moving || board.animating()) {
		board.still(); // painted again if it changed, here rather than while drawing
		source->sprites = board.sprites();
		source->width = static_cast<uint32_t>(board.pictureSize().width());
		source->height = static_cast<uint32_t>(board.pictureSize().height());
	}
}

void render(void *data, gs_effect_t *)
{
	auto *source = static_cast<Source *>(data);
	ToggleBoard &board = source->board;
	Textures &textures = source->textures;
	if (!spriteEffect) {
		spriteEffect = gs_effect_create(kSpriteEffect, "plinkotoggle-sprite.effect", nullptr);
		if (!spriteEffect)
			return;
	}

	if (!textures.still || textures.stillVersion != board.stillVersion()) {
		gs_texture_destroy(textures.still);
		textures.still = textureOf(board.still());
		textures.stillVersion = board.stillVersion();
		textures.clearButtons(); // the buttons may have changed with it
	}
	if (!textures.head)
		textures.head = textureOf(board.head());

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	const QImage &still = board.still();
	drawCentred(textures.still, QPointF(still.width() / 2.0, still.height() / 2.0), 0, 1);

	std::map<QString, gs_texture_t *> names;
	for (const ToggleBoard::Sprite &sprite : source->sprites) {
		gs_texture_t *texture = nullptr;
		switch (sprite.kind) {
		case ToggleBoard::Sprite::Kind::Head:
			texture = textures.head;
			break;
		case ToggleBoard::Sprite::Kind::Button: {
			gs_texture_t *&button = textures.buttons[sprite.slot];
			if (!button)
				button = textureOf(board.button(sprite.slot));
			texture = button;
			break;
		}
		case ToggleBoard::Sprite::Kind::Name: {
			gs_texture_t *&tag = names[sprite.name];
			if (!tag) {
				const auto kept = textures.names.find(sprite.name);
				if (kept != textures.names.end()) {
					tag = kept->second;
					textures.names.erase(kept);
				} else {
					tag = textureOf(board.name(sprite.name));
				}
			}
			texture = tag;
			break;
		}
		}
		drawCentred(texture, sprite.centre, sprite.rotation, sprite.opacity);
	}
	gs_blend_state_pop();

	// The tags of balls that are gone go too.
	for (auto &[name, texture] : textures.names)
		gs_texture_destroy(texture);
	textures.names.swap(names);
}

} // namespace

void registerBoardSource()
{
	obs_source_info info = {};
	info.id = kSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = name;
	info.create = create;
	info.destroy = destroy;
	info.get_width = width;
	info.get_height = height;
	info.activate = activate;
	info.deactivate = deactivate;
	info.video_tick = tick;
	info.video_render = render;
	info.icon_type = OBS_ICON_TYPE_GAME_CAPTURE;
	obs_register_source(&info);
}

void releaseBoardGraphics()
{
	obs_enter_graphics();
	gs_effect_destroy(spriteEffect);
	spriteEffect = nullptr;
	obs_leave_graphics();
}

void showOnBoards(const Content &content)
{
	std::lock_guard<std::mutex> hold(sourcesLock);
	if (content == shown)
		return;
	shown = content;
	for (Source *source : sources) {
		std::lock_guard<std::mutex> holdSource(source->lock);
		source->content = content;
		source->newContent = true;
	}
}

Content shownOnBoards()
{
	std::lock_guard<std::mutex> hold(sourcesLock);
	return shown;
}

int boardCount()
{
	std::lock_guard<std::mutex> hold(sourcesLock);
	return static_cast<int>(sources.size());
}

bool boardOnScreen()
{
	std::lock_guard<std::mutex> hold(sourcesLock);
	return std::any_of(sources.begin(), sources.end(), [](const Source *source) { return source->active.load(); });
}

int dropOnBoards(const QString &who, std::optional<uint32_t> seed)
{
	std::lock_guard<std::mutex> hold(sourcesLock);
	int count = 0;
	for (Source *source : sources) {
		if (!source->active)
			continue;
		std::lock_guard<std::mutex> holdSource(source->lock);
		source->drops.push_back({who, seed});
		count++;
	}
	return count;
}

void setLandingHandler(void (*handler)(const Landing &landing))
{
	landingHandler = handler;
}

void stopLandings()
{
	closing = true;
}

} // namespace sarxina::plinkotoggle
