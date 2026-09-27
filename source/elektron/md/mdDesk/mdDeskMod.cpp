#include "mdDeskMod.h"

#include <algorithm>
#include <cmath>

namespace mdDesk
{
	namespace json = elektronData::json;

	namespace
	{
		const char* curveName(const ModLink::Curve _c)
		{
			return _c == ModLink::Curve::Exp ? "exp" : _c == ModLink::Curve::Log ? "log" : "lin";
		}

		int intIn(const json::Value& _o, const char* _key, const int _lo, const int _hi, const int _default,
			const std::string& _path, std::vector<std::string>& _errors)
		{
			const auto* v = _o.find(_key);
			if(!v)
				return _default;
			if(!v->isNumber() || v->asNumber() != std::floor(v->asNumber()) || v->asNumber() < _lo || v->asNumber() > _hi)
			{
				_errors.push_back(_path + "." + _key + ": expected an integer " + std::to_string(_lo) + ".." + std::to_string(_hi));
				return _default;
			}
			return static_cast<int>(v->asNumber());
		}

		// The page's LFO shapes (mdDeskApp.js shape()), -1..1 over one cycle.
		double shape(const int _i, const double _x)
		{
			static constexpr double rnd[] = {.35, -.7, .9, -.25, .55, -.9, .1, .7};
			switch(_i)
			{
			case 0: return 1 - 4 * std::fabs(_x - .5);
			case 1: return 2 * _x - 1;
			case 2: return _x < .5 ? 1 : -1;
			case 3: return 1 - 2 * _x;
			case 4: return 2 * std::exp(-4 * _x) - 1;
			default: return rnd[static_cast<int>(_x * 8) % 8];
			}
		}
	}

	std::optional<ModSetup> modSetupFromJson(const json::Value& _doc, std::vector<std::string>& _errors)
	{
		const auto before = _errors.size();
		const auto* schema = _doc.find("schema");
		if(!schema || !schema->isString() || schema->asString() != "md-desk/modulators")
			_errors.emplace_back("$.schema: expected md-desk/modulators");
		const auto* version = _doc.find("version");
		if(!version || !version->isNumber() || version->asNumber() != 1)
			_errors.emplace_back("$.version: expected 1");
		ModSetup s;
		if(const auto* sources = _doc.find("sources"); sources && sources->isArray())
		{
			if(sources->asArray().size() > g_modMaxSources)
				_errors.push_back("$.sources: at most " + std::to_string(g_modMaxSources));
			for(size_t i = 0; i < sources->asArray().size() && i < g_modMaxSources; ++i)
			{
				const auto& o = sources->asArray()[i];
				const auto path = "$.sources[" + std::to_string(i) + "]";
				ModSource m;
				const auto* id = o.find("id");
				const auto* kind = o.find("kind");
				if(!id || !id->isString() || id->asString().empty())
					_errors.push_back(path + ".id: expected a name");
				else
					m.id = id->asString();
				if(const auto* l = o.find("label"); l && l->isString())
					m.label = l->asString();
				if(!kind || !kind->isString() || (kind->asString() != "lfo" && kind->asString() != "random"))
					_errors.push_back(path + ".kind: expected lfo or random");
				else
					m.kind = kind->asString() == "lfo" ? ModSource::Kind::Lfo : ModSource::Kind::Random;
				m.shape = static_cast<uint8_t>(intIn(o, "shape", 0, 5, 0, path, _errors));
				m.depth = static_cast<uint8_t>(intIn(o, "depth", 0, 100, 100, path, _errors));
				m.smooth = static_cast<uint8_t>(intIn(o, "smooth", 0, 127, 30, path, _errors));
				if(const auto* r = o.find("rate"))
				{
					const auto it = std::find_if(std::begin(g_modRates), std::end(g_modRates),
						[&](const ModRate& _r) { return r->isString() && r->asString() == _r.name; });
					if(it == std::end(g_modRates))
						_errors.push_back(path + ".rate: expected 1/16, 1/8, 1/4, 1/2, 1, 2 or 4");
					else
						m.rate = static_cast<uint8_t>(it - std::begin(g_modRates));
				}
				for(const auto& other : s.sources)
					if(other.id == m.id)
						_errors.push_back(path + ".id: " + m.id + " is used twice");
				s.sources.push_back(m);
			}
		}
		if(const auto* links = _doc.find("links"); links && links->isArray())
		{
			if(links->asArray().size() > g_modMaxLinks)
				_errors.push_back("$.links: at most " + std::to_string(g_modMaxLinks));
			for(size_t i = 0; i < links->asArray().size() && i < g_modMaxLinks; ++i)
			{
				const auto& o = links->asArray()[i];
				const auto path = "$.links[" + std::to_string(i) + "]";
				ModLink l;
				const auto* src = o.find("source");
				if(!src || !src->isString() || std::none_of(s.sources.begin(), s.sources.end(),
					[&](const ModSource& _s) { return _s.id == src->asString(); }))
					_errors.push_back(path + ".source: expected one of the sources");
				else
					l.source = src->asString();
				l.track = static_cast<uint8_t>(intIn(o, "track", 0, 15, 0, path, _errors));
				l.param = static_cast<uint8_t>(intIn(o, "param", 0, 23, 0, path, _errors));
				l.min = static_cast<uint8_t>(intIn(o, "min", 0, 127, 0, path, _errors));
				l.max = static_cast<uint8_t>(intIn(o, "max", 0, 127, 127, path, _errors));
				if(const auto* c = o.find("curve"))
				{
					if(!c->isString() || (c->asString() != "lin" && c->asString() != "exp" && c->asString() != "log"))
						_errors.push_back(path + ".curve: expected lin, exp or log");
					else
						l.curve = c->asString() == "exp" ? ModLink::Curve::Exp : c->asString() == "log" ? ModLink::Curve::Log
							: ModLink::Curve::Lin;
				}
				if(const auto* inv = o.find("invert"); inv && inv->isBool())
					l.invert = inv->asBool();
				s.links.push_back(l);
			}
		}
		if(_errors.size() != before)
			return {};
		return s;
	}

	json::Value modSetupToJson(const ModSetup& _s)
	{
		json::Value doc = json::Value::object();
		doc.set("schema", "md-desk/modulators");
		doc.set("version", 1);
		json::Value sources = json::Value::array();
		for(const auto& m : _s.sources)
		{
			json::Value o = json::Value::object();
			o.set("id", m.id);
			o.set("label", m.label);
			o.set("kind", m.kind == ModSource::Kind::Lfo ? "lfo" : "random");
			o.set("shape", static_cast<int>(m.shape));
			o.set("rate", g_modRates[m.rate].name);
			o.set("depth", static_cast<int>(m.depth));
			o.set("smooth", static_cast<int>(m.smooth));
			sources.push(std::move(o));
		}
		doc.set("sources", std::move(sources));
		json::Value links = json::Value::array();
		for(const auto& l : _s.links)
		{
			json::Value o = json::Value::object();
			o.set("source", l.source);
			o.set("track", static_cast<int>(l.track));
			o.set("param", static_cast<int>(l.param));
			o.set("min", static_cast<int>(l.min));
			o.set("max", static_cast<int>(l.max));
			o.set("curve", curveName(l.curve));
			o.set("invert", l.invert);
			links.push(std::move(o));
		}
		doc.set("links", std::move(links));
		return doc;
	}

	void Modulators::setSetup(ModSetup _setup)
	{
		// Keep the running values of sources that stay.
		std::vector<double> values(_setup.sources.size(), 64);
		std::vector<int> targets(_setup.sources.size(), 64);
		for(size_t i = 0; i < _setup.sources.size(); ++i)
			for(size_t j = 0; j < m_setup.sources.size(); ++j)
				if(m_setup.sources[j].id == _setup.sources[i].id)
				{
					values[i] = m_values[j];
					targets[i] = m_targets[j];
				}
		m_setup = std::move(_setup);
		m_values = std::move(values);
		m_targets = std::move(targets);
		m_sent.assign(m_setup.links.size(), -1);
	}

	void Modulators::reset()
	{
		m_phase = 0;
		std::fill(m_sent.begin(), m_sent.end(), -1);
	}

	double Modulators::sourceValue(const size_t _i)
	{
		const auto& s = m_setup.sources[_i];
		const auto n = static_cast<uint64_t>(g_modRates[s.rate].steps);
		if(s.kind == ModSource::Kind::Lfo)
		{
			const double ph = static_cast<double>(m_phase % n) / static_cast<double>(n);
			return std::clamp(std::round(64 + shape(s.shape, ph) * 63 * s.depth / 100.0), 0.0, 127.0);
		}
		if(m_phase % n == 0)
		{
			m_random ^= m_random << 13;
			m_random ^= m_random >> 17;
			m_random ^= m_random << 5;
			m_targets[_i] = static_cast<int>(m_random % 128);
		}
		const double a = 1 - s.smooth / 140.0;
		return std::clamp(std::round(m_values[_i] + (m_targets[_i] - m_values[_i]) * a), 0.0, 127.0);
	}

	std::vector<ModOutput> Modulators::step()
	{
		for(size_t i = 0; i < m_setup.sources.size(); ++i)
			m_values[i] = sourceValue(i);
		++m_phase;
		std::vector<ModOutput> out;
		for(size_t li = 0; li < m_setup.links.size(); ++li)
		{
			const auto& l = m_setup.links[li];
			const auto src = std::find_if(m_setup.sources.begin(), m_setup.sources.end(),
				[&](const ModSource& _s) { return _s.id == l.source; });
			if(src == m_setup.sources.end())
				continue;
			double x = m_values[static_cast<size_t>(src - m_setup.sources.begin())] / 127.0;
			if(l.invert)
				x = 1 - x;
			x = l.curve == ModLink::Curve::Exp ? x * x : l.curve == ModLink::Curve::Log ? std::sqrt(x) : x;
			const auto v = static_cast<int>(std::clamp(std::round(l.min + (l.max - l.min) * x), 0.0, 127.0));
			if(v == m_sent[li])
				continue;
			m_sent[li] = v;
			out.push_back({l.track, l.param, static_cast<uint8_t>(v)});
		}
		return out;
	}

	void Modulators::unsent(const ModOutput& _o)
	{
		for(size_t li = 0; li < m_setup.links.size(); ++li)
			if(m_setup.links[li].track == _o.track && m_setup.links[li].param == _o.param)
				m_sent[li] = -1;
	}

	std::vector<int> Modulators::values() const
	{
		std::vector<int> v;
		for(const auto x : m_values)
			v.push_back(static_cast<int>(x));
		return v;
	}

	bool CcBudget::take(const double _nowMs)
	{
		lastSecond(_nowMs);
		if(static_cast<int>(m_sent.size()) >= g_modCcPerSecond)
			return false;
		m_sent.push_back(_nowMs);
		return true;
	}

	int CcBudget::lastSecond(const double _nowMs)
	{
		m_sent.erase(m_sent.begin(), std::find_if(m_sent.begin(), m_sent.end(),
			[&](const double _t) { return _nowMs - _t < 1000; }));
		return static_cast<int>(m_sent.size());
	}

	std::vector<ModOutput> ModEngine::onPlayhead(const int _step, const bool _playing, const double _nowMs)
	{
		std::vector<ModOutput> out;
		if(m_playing && !_playing)
			m_mods.reset();
		m_playing = _playing;
		if(!_playing || _step < 0 || _step == m_lastStep)
		{
			m_lastStep = _playing ? m_lastStep : -1;
			return out;
		}
		m_lastStep = _step;
		if(m_mods.setup().links.empty())
			return out;
		for(const auto& o : m_mods.step())
		{
			if(m_budget.take(_nowMs))
				out.push_back(o);
			else
				m_mods.unsent(o);
		}
		return out;
	}
}
