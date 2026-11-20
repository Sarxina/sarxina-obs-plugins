#include "countdown-source.hpp"

#include "countdown-picture.hpp"

#include <obs-module.h>

#include <QImage>

#include <atomic>

namespace sarxina::keeptalking {

namespace {

// Draws a premultiplied picture, the way PlinkoToggle draws its board.
constexpr const char *kSpriteEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d image;

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
	return image.Sample(def_sampler, vert_in.uv);
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

constexpr float kPopSeconds = 0.25f;
constexpr float kPopScale = 0.18f; // 118% at the start of the pop

std::atomic<int> shownNumber{-1};
gs_effect_t *spriteEffect = nullptr; // made on the graphics thread the first time a number is drawn

struct Source {
	obs_source_t *source = nullptr;
	std::atomic<uint32_t> width{1920}, height{1080}; // the canvas's

	// The graphics thread's own.
	int number = -1;      // the number painted
	uint32_t paintedW = 0, paintedH = 0; // the canvas it was painted for
	QImage picture;
	bool newPicture = false;
	gs_texture_t *texture = nullptr;
	float popAge = kPopSeconds; // seconds since the number changed
};

const char *name(void *)
{
	return obs_module_text("SourceName");
}

void readCanvas(Source *source)
{
	obs_video_info video = {};
	if (obs_get_video_info(&video) && video.base_width && video.base_height) {
		source->width = video.base_width;
		source->height = video.base_height;
	}
}

void *create(obs_data_t *, obs_source_t *obsSource)
{
	auto *source = new Source();
	source->source = obsSource;
	readCanvas(source);
	return source;
}

void destroy(void *data)
{
	auto *source = static_cast<Source *>(data);
	obs_enter_graphics();
	gs_texture_destroy(source->texture);
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

// On the graphics thread, every frame: the number handed over, painted again when it or the canvas changed.
void tick(void *data, float seconds)
{
	auto *source = static_cast<Source *>(data);
	readCanvas(source);
	const int number = shownNumber.load();
	const uint32_t canvasWidth = source->width.load(), canvasHeight = source->height.load();
	if (number != source->number ||
	    (number >= 0 && (canvasWidth != source->paintedW || canvasHeight != source->paintedH))) {
		if (number != source->number)
			source->popAge = 0;
		source->number = number;
		source->paintedW = canvasWidth;
		source->paintedH = canvasHeight;
		source->picture = number >= 0 ? paintNumber(number, static_cast<int>(canvasWidth),
								    static_cast<int>(canvasHeight))
					      : QImage();
		source->newPicture = true;
	}
	source->popAge += seconds;
}

void render(void *data, gs_effect_t *)
{
	auto *source = static_cast<Source *>(data);
	if (source->newPicture) {
		gs_texture_destroy(source->texture);
		source->texture = nullptr;
		if (!source->picture.isNull()) {
			const uint8_t *bits = source->picture.constBits();
			source->texture = gs_texture_create(static_cast<uint32_t>(source->picture.width()),
							    static_cast<uint32_t>(source->picture.height()), GS_BGRA, 1, &bits, 0);
		}
		source->newPicture = false;
	}
	if (!source->texture)
		return;
	if (!spriteEffect) {
		spriteEffect = gs_effect_create(kSpriteEffect, "keeptalking-number.effect", nullptr);
		if (!spriteEffect)
			return;
	}

	const float pop = source->popAge < kPopSeconds ? 1.0f - source->popAge / kPopSeconds : 0.0f;
	const float scale = 1.0f + kPopScale * pop * pop;
	const uint32_t pictureWidth = gs_texture_get_width(source->texture);
	const uint32_t pictureHeight = gs_texture_get_height(source->texture);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	gs_effect_set_texture(gs_effect_get_param_by_name(spriteEffect, "image"), source->texture);
	gs_matrix_push();
	gs_matrix_translate3f(static_cast<float>(source->width) / 2, static_cast<float>(source->height) / 2, 0);
	gs_matrix_scale3f(scale, scale, 1);
	gs_matrix_translate3f(-static_cast<float>(pictureWidth) / 2, -static_cast<float>(pictureHeight) / 2, 0);
	while (gs_effect_loop(spriteEffect, "Draw"))
		gs_draw_sprite(source->texture, 0, pictureWidth, pictureHeight);
	gs_matrix_pop();
	gs_blend_state_pop();
}

} // namespace

void registerCountdownSource()
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
	info.video_tick = tick;
	info.video_render = render;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	obs_register_source(&info);
}

void releaseCountdownGraphics()
{
	obs_enter_graphics();
	gs_effect_destroy(spriteEffect);
	spriteEffect = nullptr;
	obs_leave_graphics();
}

void showNumber(int number)
{
	shownNumber = number;
}

} // namespace sarxina::keeptalking
