#include "mdAudition.h"

#include <algorithm>

namespace elektronData
{
	uint64_t AuditionMixer::play(AuditionClip _clip)
	{
		auto r = std::make_unique<Request>();
		r->clip = std::move(_clip);
		r->id = ++m_next;
		const auto id = r->id;
		m_request.store(r.get(), std::memory_order_release);
		m_owned.push_back(std::move(r));
		m_latest.store(id, std::memory_order_relaxed);
		prune();
		return id;
	}

	// The requests the audio thread can no longer read: older than the oldest it said it uses. It reads
	// only m_request and its two voices, and m_request only moves to newer ids.
	void AuditionMixer::prune()
	{
		const auto oldest = m_oldestInUse.load(std::memory_order_acquire);
		m_owned.erase(std::remove_if(m_owned.begin(), m_owned.end() - 1, [&](const std::unique_ptr<Request>& _r) { return _r->id < oldest; }),
			m_owned.end() - 1);
	}

	AuditionStatus AuditionMixer::status() const
	{
		AuditionStatus s;
		if(m_owned.empty())
			return s;
		const auto& latest = *m_owned.back();
		s.id = latest.id;
		s.playing = latest.clip.pcm && !latest.clip.pcm->empty() && latest.clip.rate
			&& m_ended.load(std::memory_order_acquire) != latest.id;
		s.position = m_taken.load(std::memory_order_acquire) == latest.id ? m_position.load(std::memory_order_relaxed) : 0;
		return s;
	}

	bool AuditionMixer::render(Voice& _v, float* _left, float* _right, const size_t _frames, const double _outRate, const float _gain, const bool _fading)
	{
		const auto& clip = _v.request->clip;
		if(!clip.pcm || !clip.rate || _outRate <= 0)
			return false;
		const auto& pcm = *clip.pcm;
		const size_t n = pcm.size();
		const double step = clip.rate / _outRate;
		constexpr float scale = 1.0f / 32768.0f;
		for(size_t f = 0; f < _frames; ++f)
		{
			const double position = static_cast<double>(_v.frame) * step;
			const auto i = static_cast<size_t>(position);
			if(i >= n || (_fading && !_v.fade))
				return false;
			const float frac = static_cast<float>(position - static_cast<double>(i));
			const float s0 = pcm[i], s1 = i + 1 < n ? pcm[i + 1] : s0;
			float v = (s0 + (s1 - s0) * frac) * scale * _gain;
			if(_fading)
				v *= static_cast<float>(_v.fade--) / g_fadeFrames;
			if(_left)
				_left[f] += v;
			if(_right)
				_right[f] += v;
			++_v.frame;
		}
		return static_cast<size_t>(static_cast<double>(_v.frame) * step) < n;
	}

	void AuditionMixer::mix(float* _left, float* _right, const size_t _frames, const double _outRate, const float _gain)
	{
		const auto* r = m_request.load(std::memory_order_acquire);
		if(r != m_voice.request)
		{
			// The one that played fades out (a click otherwise); an older fade just ends.
			if(m_voice.request && m_voice.request->clip.pcm)
				m_fading = {m_voice.request, m_voice.frame, g_fadeFrames};
			m_voice = {r, 0, 0};
			m_taken.store(r ? r->id : 0, std::memory_order_release);
			m_position.store(0, std::memory_order_relaxed);
		}
		const auto inUse = [&]
		{
			uint64_t oldest = m_voice.request ? m_voice.request->id : 0;
			if(m_fading.request)
				oldest = std::min(oldest, m_fading.request->id);
			return oldest;
		};
		m_oldestInUse.store(inUse(), std::memory_order_release);
		if(m_fading.request && !render(m_fading, _left, _right, _frames, _outRate, _gain, true))
			m_fading = {};
		if(m_voice.request && m_ended.load(std::memory_order_relaxed) != m_voice.request->id)
		{
			if(!render(m_voice, _left, _right, _frames, _outRate, _gain, false))
				m_ended.store(m_voice.request->id, std::memory_order_release);
			const auto& clip = m_voice.request->clip;
			const double played = clip.rate && _outRate > 0 ? static_cast<double>(m_voice.frame) * clip.rate / _outRate : 0;
			m_position.store(static_cast<uint32_t>(std::min<double>(played, clip.pcm ? static_cast<double>(clip.pcm->size()) : 0)), std::memory_order_relaxed);
		}
		m_oldestInUse.store(inUse(), std::memory_order_release);
	}
}
