#include "word-count-source.hpp"

#include <obs-module.h>

#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QLocale>
#include <QPainter>

#include <algorithm>

#include <atomic>

namespace sarxina::keeptalking {

namespace {

constexpr int kTextSize = 64; // px; the streamer scales the source in the scene
const QString kIcon = QString::fromUtf8("\xF0\x9F\x97\xA3"); // speaking head

std::atomic<long long> shownWords{0};

struct Source {
	obs_source_t *source = nullptr;
	std::atomic<uint32_t> width{1}, height{1}; // the picture's

	// The graphics thread's own.
	long long words = -1; // the count painted
	QImage picture;
	bool newPicture = false;
	gs_texture_t *texture = nullptr;
};

QString wordsLine(long long words)
{
	return QStringLiteral("%1 %2").arg(QLocale(QLocale::English).toString(words),
					   QString::fromUtf8(obs_module_text(words == 1 ? "WordCount.Word"
											: "WordCount.Words")));
}

// The emoji, then the count in white with a soft dark shadow, on a transparent picture just big enough.
QImage paintCount(long long words)
{
	QFont font(QStringLiteral("Segoe UI"));
	font.setPixelSize(kTextSize);
	font.setWeight(QFont::DemiBold);
	QFont emoji(QStringLiteral("Segoe UI Emoji"));
	emoji.setPixelSize(kTextSize);
	const QFontMetrics text(font), icon(emoji);
	const QString line = wordsLine(words);

	const int margin = 4, shadow = std::max(2, kTextSize / 20), gap = kTextSize / 4;
	const int iconWidth = icon.horizontalAdvance(kIcon);
	const int ascent = std::max(text.ascent(), icon.ascent()), descent = std::max(text.descent(), icon.descent());
	QImage picture(2 * margin + iconWidth + gap + text.horizontalAdvance(line) + shadow,
		       2 * margin + ascent + descent + shadow, QImage::Format_ARGB32_Premultiplied);
	picture.fill(Qt::transparent);

	QPainter painter(&picture);
	painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
	const int baseline = margin + ascent, x = margin + iconWidth + gap;
	painter.setFont(emoji);
	painter.setPen(Qt::white);
	painter.drawText(margin, baseline, kIcon);
	painter.setFont(font);
	painter.setPen(QColor(0, 0, 0, 120));
	painter.drawText(x + shadow / 2, baseline + shadow, line);
	painter.setPen(QColor(255, 255, 255, 240));
	painter.drawText(x, baseline, line);
	return picture;
}

const char *name(void *)
{
	return obs_module_text("WordCountSourceName");
}

void *create(obs_data_t *, obs_source_t *obsSource)
{
	auto *source = new Source();
	source->source = obsSource;
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

obs_properties_t *properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_text(properties, "about", obs_module_text("WordCount.About"), OBS_TEXT_INFO);
	return properties;
}

// On the graphics thread, every frame: painted again when the count changed.
void tick(void *data, float)
{
	auto *source = static_cast<Source *>(data);
	const long long words = shownWords.load();
	if (words == source->words)
		return;
	source->words = words;
	source->picture = paintCount(words);
	source->width = static_cast<uint32_t>(source->picture.width());
	source->height = static_cast<uint32_t>(source->picture.height());
	source->newPicture = true;
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
	// The picture is premultiplied: OBS's default effect with "one, one minus source alpha" blending draws it.
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), source->texture);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(source->texture, 0, 0, 0);
	gs_blend_state_pop();
}

} // namespace

void registerWordCountSource()
{
	obs_source_info info = {};
	info.id = kWordCountSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = name;
	info.create = create;
	info.destroy = destroy;
	info.get_width = width;
	info.get_height = height;
	info.get_properties = properties;
	info.video_tick = tick;
	info.video_render = render;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	obs_register_source(&info);
}

void showWordCount(long long words)
{
	shownWords = words;
}

} // namespace sarxina::keeptalking
