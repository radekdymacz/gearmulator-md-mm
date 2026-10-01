#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace elektronData
{
	// P9: a sample slot heard once on the plug-in's own output (data-contract.md 4.9, audition). Pure:
	// the emulated machine's device owns one and mixes it into its main output, so a DAW hears it too.

	// What to play: 16-bit mono at its own rate. A null pcm stops.
	struct AuditionClip
	{
		std::shared_ptr<const std::vector<int16_t>> pcm;
		uint32_t rate = 0;
	};

	// The latest request as the mixer sees it. id 0: none (or another mixer: a new device).
	struct AuditionStatus
	{
		uint64_t id = 0;
		bool playing = false;		// the request plays (or is about to): not stopped, not played out
		uint32_t position = 0;		// samples of the clip played
	};

	// One audition at a time. play and stop on one control thread (the device lock's), mix on the audio
	// thread. The audio thread takes no lock and allocates or frees nothing: the control thread owns every
	// request and frees one only once the audio thread has said it no longer uses it.
	class AuditionMixer
	{
	public:
		AuditionMixer() = default;
		AuditionMixer(const AuditionMixer&) = delete;
		AuditionMixer& operator=(const AuditionMixer&) = delete;

		// Control thread. The request's id (> 0); a new one replaces what plays (a short fade).
		uint64_t play(AuditionClip _clip);
		uint64_t stop() { return play({}); }
		AuditionStatus status() const;
		size_t held() const { return m_owned.size(); }		// requests not freed yet (tests)

		// Audio thread: adds the clip to _left and _right (either may be null), resampled from its rate
		// to _outRate (linear), at _gain.
		void mix(float* _left, float* _right, size_t _frames, double _outRate, float _gain = 1.0f);

		static constexpr uint32_t g_fadeFrames = 64;

	private:
		struct Request
		{
			AuditionClip clip;
			uint64_t id = 0;
		};
		// One voice of the audio thread.
		struct Voice
		{
			const Request* request = nullptr;
			uint64_t frame = 0;		// output frames played (the clip's position is frame * its step: no drift)
			uint32_t fade = 0;		// frames left of a fade out (0: none)
		};
		bool render(Voice& _v, float* _left, float* _right, size_t _frames, double _outRate, float _gain, bool _fading);
		void prune();

		// control thread
		std::vector<std::unique_ptr<Request>> m_owned;
		uint64_t m_next = 0;
		std::atomic<uint64_t> m_latest{0};

		// shared
		std::atomic<const Request*> m_request{nullptr};
		std::atomic<uint64_t> m_oldestInUse{0};		// the lowest id the audio thread may still read
		std::atomic<uint64_t> m_taken{0};			// the latest id the audio thread took
		std::atomic<uint64_t> m_ended{0};			// an id that played out
		std::atomic<uint32_t> m_position{0};

		// audio thread
		Voice m_voice, m_fading;
	};
}
