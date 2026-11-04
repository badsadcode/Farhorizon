#pragma once
// Software audio mixer (design doc 12): mono sound buffers, voices with loop/pitch/pan/gain,
// buses with fades. Output is interleaved stereo float at 48 kHz. No SDL/OS dependency, so it is
// unit-tested headlessly; engine/audio/audio_device.* feeds it to the sound card.
// Thread safety: all public methods lock an internal mutex (game thread vs audio callback).
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace fh {

inline constexpr int kAudioRate = 48000;

struct SoundBuffer {
  std::vector<float> samples;  // mono, kAudioRate
  float seconds() const { return float(samples.size()) / float(kAudioRate); }
};

// 16-bit PCM WAV (mono or stereo, any rate) -> mono 48 kHz float. Errors are reported.
std::optional<SoundBuffer> loadWav(const std::filesystem::path& path, std::string* error = nullptr);

enum class AudioBus { Ambience, Effects, Engine, Ui, Count };

using SoundId = std::uint32_t;
using VoiceId = std::uint32_t;
inline constexpr VoiceId kNoVoice = 0;

struct PlayParams {
  float gain = 1.0f;
  float pitch = 1.0f;
  float pan = 0.0f;      // -1 left .. 1 right
  bool loop = false;
  AudioBus bus = AudioBus::Effects;
  float fadeIn = 0.0f;   // seconds
};

class Mixer {
 public:
  SoundId addSound(SoundBuffer buffer);
  VoiceId play(SoundId sound, const PlayParams& p);
  void stop(VoiceId v, float fadeOut = 0.05f);
  void setGain(VoiceId v, float gain, float fadeSeconds = 0.1f);
  void setPitch(VoiceId v, float pitch);
  bool playing(VoiceId v) const;
  void setBusGain(AudioBus bus, float gain, float fadeSeconds = 0.3f);
  void setMasterGain(float g) { std::lock_guard l(m_); master_ = g; }
  int activeVoices() const;

  // Mixes `frames` stereo frames into `out` (overwrites). Called from the audio thread.
  void mix(float* out, int frames);

 private:
  struct Ramp {
    float value = 1, target = 1, step = 0;
    void set(float t, float seconds) {
      target = t;
      step = seconds > 0 ? (t - value) / (seconds * float(kAudioRate)) : 0;
      if (seconds <= 0) value = t;
    }
    float next() {
      if (step != 0) {
        value += step;
        if ((step > 0 && value >= target) || (step < 0 && value <= target)) {
          value = target;
          step = 0;
        }
      }
      return value;
    }
  };
  struct Voice {
    VoiceId id = 0;
    SoundId sound = 0;
    double pos = 0;
    float pitch = 1, pan = 0;
    bool loop = false, stopping = false;
    AudioBus bus = AudioBus::Effects;
    Ramp gain;
  };
  mutable std::mutex m_;
  std::vector<SoundBuffer> sounds_;
  std::vector<Voice> voices_;
  Ramp busGain_[int(AudioBus::Count)];
  float master_ = 0.8f;
  VoiceId nextId_ = 1;
};

}  // namespace fh
