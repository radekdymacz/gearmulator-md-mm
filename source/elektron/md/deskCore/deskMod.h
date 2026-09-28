#pragma once

#include "elektronData/json.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deskCore
{
	// App-only modulation sources (the Control workspace's App LFO and Random rows).
	// They run in the editor, not in the Machinedrum: on every step of the
	// machine's own playhead each source moves, and every link turns the source's
	// value into a kit parameter through min, max and a curve. The desk sends the
	// results as CCs, like host automation, within a CC-rate budget. A real
	// Machinedrum would not play them, and they are not saved in the kit.
	//
	// The page owns the setup and sends it whole ("md-desk/modulators", "mm-desk/modulators"; see
	// doc/modern-ux/data-contract.md). Shared by both editors (P6): a model says what its links
	// may address (ModLimits). Pure: no clock, no I/O.
	struct ModSource
	{
		enum class Kind : uint8_t
		{
			Lfo,
			Random
		};

		std::string id;
		std::string label;
		Kind kind = Kind::Lfo;
		uint8_t shape = 0;		// LFO: 0 triangle, 1 saw, 2 square, 3 linear decay, 4 exp decay, 5 random
		uint8_t rate = 3;		// index into g_modRates: the cycle (LFO) or hold (random) length
		uint8_t depth = 100;	// LFO: percent
		uint8_t smooth = 30;	// random: 0 jumps .. 127 glides

		bool operator==(const ModSource& _o) const
		{
			return id == _o.id && label == _o.label && kind == _o.kind && shape == _o.shape && rate == _o.rate
				&& depth == _o.depth && smooth == _o.smooth;
		}
	};

	struct ModLink
	{
		enum class Curve : uint8_t
		{
			Lin,
			Exp,
			Log
		};

		std::string source;
		uint8_t track = 0;
		uint8_t param = 0;		// the model's kit parameter (MD 0-23; MM DATA page * 8 + index)
		uint8_t min = 0;
		uint8_t max = 127;
		Curve curve = Curve::Lin;
		bool invert = false;

		bool operator==(const ModLink& _o) const
		{
			return source == _o.source && track == _o.track && param == _o.param && min == _o.min && max == _o.max
				&& curve == _o.curve && invert == _o.invert;
		}
	};

	struct ModSetup
	{
		std::vector<ModSource> sources;
		std::vector<ModLink> links;
	};

	// Rates as the mockup names them, in 16th-note steps.
	struct ModRate
	{
		const char* name;
		int steps;
	};
	constexpr ModRate g_modRates[] = {{"1/16", 1}, {"1/8", 2}, {"1/4", 4}, {"1/2", 8}, {"1", 16}, {"2", 32}, {"4", 64}};
	constexpr size_t g_modMaxSources = 16;
	constexpr size_t g_modMaxLinks = 64;
	// The mockup marks more than 300 CCs a second as too many for the machine.
	constexpr int g_modCcPerSecond = 300;

	// What a model's links may address, and its setup document's schema name.
	struct ModLimits
	{
		const char* schema = "md-desk/modulators";
		int maxTrack = 15;
		int maxParam = 23;
	};

	std::optional<ModSetup> modSetupFromJson(const elektronData::json::Value& _doc, std::vector<std::string>& _errors,
		const ModLimits& _limits = {});
	elektronData::json::Value modSetupToJson(const ModSetup& _setup, const ModLimits& _limits = {});

	struct ModOutput
	{
		uint8_t track;
		uint8_t param;
		uint8_t value;
	};

	class Modulators
	{
	public:
		void setSetup(ModSetup _setup);
		const ModSetup& setup() const { return m_setup; }

		// One step of the machine's playhead: moves every source, returns the link
		// values that changed since they were last sent.
		std::vector<ModOutput> step();
		// The machine stopped: sources restart from phase 0 on the next PLAY.
		void reset();
		// What a link's output did not get sent (over the CC budget): send it again.
		void unsent(const ModOutput& _o);

		// Current source values 0-127, in setup order.
		std::vector<int> values() const;

	private:
		double sourceValue(size_t _i);

		ModSetup m_setup;
		std::vector<double> m_values;
		std::vector<int> m_targets;		// random: the value it glides to
		std::vector<int> m_sent;		// per link, -1 = never sent
		uint64_t m_phase = 0;
		uint32_t m_random = 0x2545f491;
	};

	// Rolling one-second CC budget.
	class CcBudget
	{
	public:
		bool take(double _nowMs);
		int lastSecond(double _nowMs);

	private:
		std::vector<double> m_sent;
	};

	// The app modulators as they run (P5): the setup, the machine's steps in, the CCs to send
	// out within the budget. Pure; the editor's desk or the plug-in's processor (so they run with
	// the editor closed) drives one.
	class ModEngine
	{
	public:
		void setSetup(ModSetup _setup) { m_mods.setSetup(std::move(_setup)); }
		const ModSetup& setup() const { return m_mods.setup(); }

		// The machine's playhead: a new step while playing moves the sources; the link values
		// to send now come back (over the budget they wait for a later step). Stopping restarts
		// the sources from phase 0.
		std::vector<ModOutput> onPlayhead(int _step, bool _playing, double _nowMs);

		std::vector<int> values() const { return m_mods.values(); }
		int ccPerSecond(const double _nowMs) { return m_budget.lastSecond(_nowMs); }

		// {"type":"mod","doc","values","ccPerSecond","runs":"plug-in","ccLimit"}: what the page shows.
		elektronData::json::Value message(const ModLimits& _limits, double _nowMs);

		// One step of the machine (both desks, on the adapter's step edge): the modulated values go
		// to the adapter (sendModulation, like host automation); the message to publish while any
		// modulator runs, and until the CC rate it showed has fallen to zero after the last one went.
		template<typename Adapter, typename Documents>
		std::optional<elektronData::json::Value> step(Adapter& _adapter, const Documents& _view, const ModLimits& _limits,
			const int _step, const bool _playing, const double _nowMs)
		{
			for(const auto& o : onPlayhead(_step, _playing, _nowMs))
				_adapter.sendModulation(o.track, o.param, o.value, _view);
			if(setup().sources.empty() && m_shownRate == 0)
				return std::nullopt;
			auto m = message(_limits, _nowMs);
			m_shownRate = ccPerSecond(_nowMs);
			return m;
		}

	private:
		Modulators m_mods;
		CcBudget m_budget;
		int m_shownRate = 0;	// the CC rate the last stepped message showed
		int m_lastStep = -1;
		bool m_playing = false;
	};


}
