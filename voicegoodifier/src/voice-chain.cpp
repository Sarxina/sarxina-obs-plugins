#include "voice-chain.hpp"

#include "eq-settings.hpp"
#include "gate-settings.hpp"
#include "limiter-settings.hpp"

#include <obs-module.h>

#include <algorithm>

namespace sarxina::voice {

namespace {

// The gate's or the limiter's properties, in a group of their own when the chain has more than one stage.
obs_properties_t *stageGroup(obs_properties_t *props, const Layout &layout, const char *name, const char *title)
{
	if (!layout.chain())
		return props;
	obs_properties_t *group = obs_properties_create();
	obs_properties_add_group(props, name, obs_module_text(title), OBS_GROUP_NORMAL, group);
	return group;
}

} // namespace

const char *Layout::prefix(Stage stage) const
{
	if (!chain())
		return "";
	switch (stage) {
	case GateStage:
		return "gate_";
	case EqStage:
		return "eq_";
	default:
		return "limiter_";
	}
}

void setDefaults(obs_data_t *data, const Layout &layout)
{
	if (layout.has(GateStage))
		gate::setDefaults(data, layout.prefix(GateStage));
	if (layout.has(EqStage))
		eq::setDefaults(data, layout.prefix(EqStage),
				layout.chain() ? eq::Settings::streamVoice() : eq::Settings::defaults());
	if (layout.has(LimiterStage))
		limiter::setDefaults(data, layout.prefix(LimiterStage));
}

ChainSettings read(obs_data_t *data, const Layout &layout, double sampleRate)
{
	ChainSettings s;
	if (layout.has(GateStage))
		s.gate = gate::read(data, layout.prefix(GateStage));
	if (layout.has(EqStage))
		s.eq = eq::Design::of(eq::read(data, layout.prefix(EqStage)), sampleRate);
	if (layout.has(LimiterStage))
		s.limiter = limiter::read(data, layout.prefix(LimiterStage));
	return s;
}

void addStageProperties(obs_properties_t *props, const Layout &layout)
{
	if (layout.has(GateStage))
		gate::addProperties(stageGroup(props, layout, "gate", "GateName"), layout.prefix(GateStage));
	if (layout.has(LimiterStage))
		limiter::addProperties(stageGroup(props, layout, "limiter", "LimiterName"), layout.prefix(LimiterStage));
}

Chain::Chain(const Layout &layout, double sampleRate)
{
	if (layout.has(GateStage))
		gate_ = std::make_unique<gate::Processor>(sampleRate);
	if (layout.has(EqStage))
		eq_ = std::make_unique<eq::Processor>();
	if (layout.has(LimiterStage))
		limiter_ = std::make_unique<limiter::Processor>(sampleRate);
}

void Chain::setSettings(const ChainSettings &settings)
{
	if (gate_)
		gate_->setSettings(settings.gate);
	if (eq_)
		eq_->setDesign(settings.eq);
	if (limiter_)
		limiter_->setSettings(settings.limiter);
}

void Chain::process(float **channels, int count, size_t frames)
{
	if (gate_)
		gate_->process(channels, count, frames);
	if (eq_) {
		for (int c = 0; c < std::min(count, eq::Processor::maxChannels); c++)
			eq_->process(channels[c], frames, c);
	}
	if (limiter_)
		limiter_->process(channels, count, frames);
}

int Chain::latencySamples() const
{
	return (gate_ ? gate_->latencySamples() : 0) + (limiter_ ? limiter_->latencySamples() : 0);
}

} // namespace sarxina::voice
