#include "engine/audio/mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/core/file_io.h"

namespace fh {

std::optional<SoundBuffer> loadWav(const std::filesystem::path& path, std::string* error) {
  auto fail = [&](const std::string& m) -> std::optional<SoundBuffer> {
    if (error) *error = path.string() + ": " + m;
    return std::nullopt;
  };
  auto bytes = readBinaryFile(path);
  if (!bytes) return fail("cannot read");
  const auto& b = *bytes;
  auto u32 = [&](size_t o) { std::uint32_t v; std::memcpy(&v, &b[o], 4); return v; };
  auto u16 = [&](size_t o) { std::uint16_t v; std::memcpy(&v, &b[o], 2); return v; };
  if (b.size() < 12 || std::memcmp(&b[0], "RIFF", 4) != 0 || std::memcmp(&b[8], "WAVE", 4) != 0)
    return fail("not a RIFF/WAVE file");
  int channels = 0, rate = 0, bits = 0;
  size_t dataOff = 0, dataLen = 0;
  for (size_t o = 12; o + 8 <= b.size();) {
    const std::uint32_t len = u32(o + 4);
    if (std::memcmp(&b[o], "fmt ", 4) == 0 && o + 24 <= b.size()) {
      if (u16(o + 8) != 1) return fail("only PCM WAV is supported");
      channels = u16(o + 10);
      rate = int(u32(o + 12));
      bits = u16(o + 22);
    } else if (std::memcmp(&b[o], "data", 4) == 0) {
      dataOff = o + 8;
      dataLen = std::min<size_t>(len, b.size() - dataOff);
    }
    o += 8 + len + (len & 1);
  }
  if (bits != 16 || channels < 1 || channels > 2 || rate <= 0 || !dataOff) return fail("need 16-bit mono/stereo PCM");
  const size_t frames = dataLen / (2 * size_t(channels));
  std::vector<float> mono(frames);
  for (size_t i = 0; i < frames; ++i) {
    float s = 0;
    for (int c = 0; c < channels; ++c) {
      std::int16_t v;
      std::memcpy(&v, &b[dataOff + (i * size_t(channels) + size_t(c)) * 2], 2);
      s += float(v) / 32768.0f;
    }
    mono[i] = s / float(channels);
  }
  SoundBuffer out;
  if (rate == kAudioRate) {
    out.samples = std::move(mono);
  } else {  // linear resample to 48 kHz
    const double ratio = double(rate) / double(kAudioRate);
    const size_t n = size_t(double(frames) / ratio);
    out.samples.resize(n);
    for (size_t i = 0; i < n; ++i) {
      const double src = double(i) * ratio;
      const size_t a = size_t(src);
      const size_t c = std::min(a + 1, frames - 1);
      const float f = float(src - double(a));
      out.samples[i] = mono[a] * (1 - f) + mono[c] * f;
    }
  }
  return out;
}

SoundId Mixer::addSound(SoundBuffer buffer) {
  std::lock_guard l(m_);
  sounds_.push_back(std::move(buffer));
  return SoundId(sounds_.size());  // 1-based; 0 = invalid
}

VoiceId Mixer::play(SoundId sound, const PlayParams& p) {
  std::lock_guard l(m_);
  if (sound == 0 || sound > sounds_.size() || sounds_[sound - 1].samples.empty()) return kNoVoice;
  if (voices_.size() >= 64) return kNoVoice;  // hard cap
  Voice v;
  v.id = nextId_++;
  v.sound = sound;
  v.pitch = std::max(0.05f, p.pitch);
  v.pan = std::clamp(p.pan, -1.0f, 1.0f);
  v.loop = p.loop;
  v.bus = p.bus;
  v.gain.value = p.fadeIn > 0 ? 0.0f : p.gain;
  v.gain.set(p.gain, p.fadeIn);
  voices_.push_back(v);
  return v.id;
}

void Mixer::stop(VoiceId id, float fadeOut) {
  std::lock_guard l(m_);
  for (auto& v : voices_)
    if (v.id == id) {
      v.stopping = true;
      v.gain.set(0.0f, std::max(0.001f, fadeOut));
    }
}

void Mixer::setGain(VoiceId id, float gain, float fadeSeconds) {
  std::lock_guard l(m_);
  for (auto& v : voices_)
    if (v.id == id && !v.stopping) v.gain.set(gain, fadeSeconds);
}

void Mixer::setPitch(VoiceId id, float pitch) {
  std::lock_guard l(m_);
  for (auto& v : voices_)
    if (v.id == id) v.pitch = std::max(0.05f, pitch);
}

bool Mixer::playing(VoiceId id) const {
  std::lock_guard l(m_);
  for (const auto& v : voices_)
    if (v.id == id) return true;
  return false;
}

void Mixer::setBusGain(AudioBus bus, float gain, float fadeSeconds) {
  std::lock_guard l(m_);
  busGain_[int(bus)].set(gain, fadeSeconds);
}

int Mixer::activeVoices() const {
  std::lock_guard l(m_);
  return int(voices_.size());
}

void Mixer::mix(float* out, int frames) {
  std::lock_guard l(m_);
  std::fill(out, out + size_t(frames) * 2, 0.0f);
  for (int f = 0; f < frames; ++f) {
    float bus[int(AudioBus::Count)];
    for (int b = 0; b < int(AudioBus::Count); ++b) bus[b] = busGain_[b].next();
    float L = 0, R = 0;
    for (auto& v : voices_) {
      const auto& s = sounds_[v.sound - 1].samples;
      if (v.pos >= double(s.size())) continue;
      const size_t i = size_t(v.pos);
      const float frac = float(v.pos - double(i));
      size_t j = i + 1;
      if (j >= s.size()) j = v.loop ? 0 : i;
      const float x = (s[i] * (1 - frac) + s[j] * frac) * v.gain.next() * bus[int(v.bus)];
      // Equal-power pan.
      const float a = (v.pan + 1.0f) * 0.25f * 3.14159265f;
      L += x * std::cos(a);
      R += x * std::sin(a);
      v.pos += v.pitch;
      if (v.pos >= double(s.size()) && v.loop) v.pos -= double(s.size());
    }
    out[2 * f] = std::tanh(L * master_);  // soft clip, never harsh digital clipping
    out[2 * f + 1] = std::tanh(R * master_);
  }
  // Retire finished voices.
  voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                               [&](const Voice& v) {
                                 const bool ended = !v.loop && v.pos >= double(sounds_[v.sound - 1].samples.size());
                                 const bool faded = v.stopping && v.gain.value <= 0.0f && v.gain.step == 0;
                                 return ended || faded;
                               }),
                voices_.end());
}

}  // namespace fh
