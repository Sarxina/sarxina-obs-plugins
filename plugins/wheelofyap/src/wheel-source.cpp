#include "wheel-source.hpp"

#include "wheel.hpp"

#include <obs-module.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <vector>

namespace sarxina::wheelofyap {

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

struct Spin {
	std::shared_ptr<const Face> face;
	Plan plan;
};

struct Textures {
	gs_texture_t *disc = nullptr;
	const Face *discOf = nullptr;
	gs_texture_t *banner = nullptr, *wedge = nullptr;
	uint64_t planOf = 0;
	gs_texture_t *hub = nullptr, *pointer = nullptr;

	void destroy()
	{
		gs_texture_destroy(disc);
		gs_texture_destroy(banner);
		gs_texture_destroy(wedge);
		gs_texture_destroy(hub);
		gs_texture_destroy(pointer);
		disc = banner = wedge = hub = pointer = nullptr;
		discOf = nullptr;
		planOf = 0;
	}
};

struct Source {
	obs_source_t *source = nullptr;
	std::atomic<bool> active{false};

	// Handed over from the UI thread, under `lock`.
	std::mutex lock;
	std::shared_ptr<const Face> newFace;
	std::optional<Spin> newSpin;

	// The graphics thread's own.
	Spinner spinner;
	std::shared_ptr<const Face> waitingFace; // new topics, shown once the spin that is turning stops
	Textures textures;
};

std::mutex sourcesLock; // guards the three below
std::vector<Source *> sources;
std::shared_ptr<const Face> shownFace;
void (*landingHandler)(const Landed &) = nullptr;
void (*editTopicsHandler)() = nullptr;

QStringList shownSlices;
std::atomic<bool> closing{false};
gs_effect_t *spriteEffect = nullptr;
QImage hubPicture, pointerPicture; // painted once, on the UI thread

void handLanding(void *param)
{
	std::unique_ptr<Landed> landed(static_cast<Landed *>(param));
	if (!closing && landingHandler)
		landingHandler(*landed);
}

gs_texture_t *textureOf(const QImage &image)
{
	if (image.isNull())
		return nullptr;
	const uint8_t *bits = image.constBits();
	return gs_texture_create(static_cast<uint32_t>(image.width()), static_cast<uint32_t>(image.height()), GS_BGRA, 1,
				 &bits, 0);
}

// Draws a picture with its top left at (x, y) of the matrix as it is.
void drawAt(gs_texture_t *texture, double x, double y, double opacity)
{
	if (!texture || opacity <= 0)
		return;
	gs_effect_set_texture(gs_effect_get_param_by_name(spriteEffect, "image"), texture);
	gs_effect_set_float(gs_effect_get_param_by_name(spriteEffect, "opacity"), static_cast<float>(opacity));
	gs_matrix_push();
	gs_matrix_translate3f(static_cast<float>(x), static_cast<float>(y), 0);
	while (gs_effect_loop(spriteEffect, "Draw"))
		gs_draw_sprite(texture, 0, gs_texture_get_width(texture), gs_texture_get_height(texture));
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
	std::lock_guard<std::mutex> hold(sourcesLock);
	source->newFace = shownFace;
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

uint32_t width(void *)
{
	return kSize;
}

uint32_t height(void *)
{
	return kSize;
}

void activate(void *data)
{
	static_cast<Source *>(data)->active = true;
}

void deactivate(void *data)
{
	static_cast<Source *>(data)->active = false;
}

bool editPressed(obs_properties_t *, obs_property_t *, void *)
{
	if (editTopicsHandler)
		editTopicsHandler();
	return false;
}

obs_properties_t *properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_button2(properties, "topics_edit", obs_module_text("Topics.Edit"), editPressed, nullptr);
	return properties;
}

void tick(void *data, float seconds)
{
	auto *source = static_cast<Source *>(data);
	std::optional<Spin> spin;
	std::shared_ptr<const Face> face;
	{
		std::lock_guard<std::mutex> hold(source->lock);
		spin.swap(source->newSpin);
		face.swap(source->newFace);
	}
	if (face)
		source->waitingFace = face;
	if (spin) {
		source->spinner.start(spin->face, spin->plan);
		source->waitingFace.reset();
	}
	if (source->waitingFace && !source->spinner.spinning()) {
		source->spinner.show(source->waitingFace);
		source->waitingFace.reset();
	}
	if (source->spinner.advance(seconds) && !closing) {
		const Plan &plan = source->spinner.plan();
		obs_queue_task(OBS_TASK_UI, handLanding, new Landed{plan.id, plan.who, plan.topic, plan.forced}, false);
	}
}

void render(void *data, gs_effect_t *)
{
	auto *source = static_cast<Source *>(data);
	const Spinner &spinner = source->spinner;
	Textures &textures = source->textures;
	if (!spriteEffect) {
		spriteEffect = gs_effect_create(kSpriteEffect, "wheelofyap-sprite.effect", nullptr);
		if (!spriteEffect)
			return;
	}
	const auto &face = spinner.face();
	if (!face)
		return;
	if (textures.discOf != face.get()) {
		gs_texture_destroy(textures.disc);
		textures.disc = textureOf(face->disc);
		textures.discOf = face.get();
	}
	if (textures.planOf != spinner.plan().id) {
		gs_texture_destroy(textures.banner);
		gs_texture_destroy(textures.wedge);
		textures.banner = textureOf(spinner.plan().banner);
		textures.wedge = textureOf(spinner.plan().wedge);
		textures.planOf = spinner.plan().id;
	}
	if (!textures.hub)
		textures.hub = textureOf(hubPicture);
	if (!textures.pointer)
		textures.pointer = textureOf(pointerPicture);

	const auto x = [](double value) { return static_cast<float>(value); };
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	// The wheel and its lit slice, turned about the centre.
	gs_matrix_push();
	gs_matrix_translate3f(x(kCentre.x()), x(kCentre.y()), 0);
	gs_matrix_rotaa4f(0, 0, 1, x(spinner.angle()));
	drawAt(textures.disc, -kCentre.x(), -kCentre.y(), 1);
	drawAt(textures.wedge, spinner.plan().wedgeAt.x(), spinner.plan().wedgeAt.y(), spinner.wedgeOpacity());
	gs_matrix_pop();

	drawAt(textures.hub, kCentre.x() - hubPicture.width() / 2.0, kCentre.y() - hubPicture.height() / 2.0, 1);

	gs_matrix_push();
	gs_matrix_translate3f(x(kPointerAt.x()), x(kPointerAt.y()), 0);
	gs_matrix_rotaa4f(0, 0, 1, x(spinner.pointer()));
	drawAt(textures.pointer, -kPointerPivot.x(), -kPointerPivot.y(), 1);
	gs_matrix_pop();

	if (const double scale = spinner.bannerScale(); scale > 0 && textures.banner) {
		gs_matrix_push();
		gs_matrix_translate3f(x(kBannerAt.x()), x(kBannerAt.y()), 0);
		gs_matrix_scale3f(x(scale), x(scale), 1);
		drawAt(textures.banner, -spinner.plan().banner.width() / 2.0, -spinner.plan().banner.height() / 2.0, 1);
		gs_matrix_pop();
	}
	gs_blend_state_pop();
}

} // namespace

void registerWheelSource()
{
	hubPicture = paintHub();
	pointerPicture = paintPointer();
	shownFace = paintFace({});

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
	info.get_properties = properties;
	info.video_tick = tick;
	info.video_render = render;
	info.icon_type = OBS_ICON_TYPE_GAME_CAPTURE;
	obs_register_source(&info);
}

void releaseWheelGraphics()
{
	obs_enter_graphics();
	gs_effect_destroy(spriteEffect);
	spriteEffect = nullptr;
	obs_leave_graphics();
}

void showTopics(const QStringList &slices)
{
	if (slices == shownSlices && shownFace)
		return;
	shownSlices = slices;
	auto face = paintFace(slices);
	std::lock_guard<std::mutex> hold(sourcesLock);
	shownFace = face;
	for (Source *source : sources) {
		std::lock_guard<std::mutex> holdSource(source->lock);
		source->newFace = face;
	}
}

int wheelCount()
{
	std::lock_guard<std::mutex> hold(sourcesLock);
	return static_cast<int>(sources.size());
}

bool wheelOnScreen()
{
	std::lock_guard<std::mutex> hold(sourcesLock);
	return std::any_of(sources.begin(), sources.end(), [](const Source *source) { return source->active.load(); });
}

int spinWheels(uint64_t id, const QString &who, const QString &forced, uint32_t seed)
{
	std::shared_ptr<const Face> face;
	{
		std::lock_guard<std::mutex> hold(sourcesLock);
		face = shownFace;
	}
	if (!forced.isEmpty()) {
		std::mt19937 random(seed ^ 0x9E3779B9u);
		const int at = std::uniform_int_distribution<int>(0, static_cast<int>(shownSlices.size()))(random);
		face = paintFace(shownSlices, forced, at);
	}
	Plan plan = planSpin(*face, who, seed);
	plan.id = id;
	std::lock_guard<std::mutex> hold(sourcesLock);
	int count = 0;
	for (Source *source : sources) {
		if (!source->active)
			continue;
		std::lock_guard<std::mutex> holdSource(source->lock);
		source->newSpin = Spin{face, plan};
		source->newFace.reset();
		count++;
	}
	return count;
}

void setLandingHandler(void (*handler)(const Landed &landed))
{
	landingHandler = handler;
}

void setEditTopicsHandler(void (*handler)())
{
	editTopicsHandler = handler;
}

void stopLandings()
{
	closing = true;
}

} // namespace sarxina::wheelofyap
