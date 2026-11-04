#include <cmath>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "engine/audio/mixer.h"
#include "game/audio_director.h"
#include "test_framework.h"

using namespace fh;

namespace {
SoundBuffer tone(float seconds, float hz) {
  SoundBuffer b;
  b.samples.resize(size_t(seconds * kAudioRate));
  for (size_t i = 0; i < b.samples.size(); ++i) b.samples[i] = 0.5f * std::sin(6.2831853f * hz * float(i) / kAudioRate);
  return b;
}
float rms(const std::vector<float>& v) {
  double s = 0;
  for (float x : v) s += double(x) * x;
  return float(std::sqrt(s / double(v.size())));
}
}  // namespace

TEST(mixer_one_shot_ends_and_loop_continues) {
  Mixer m;
  const SoundId s = m.addSound(tone(0.1f, 440));
  const VoiceId shot = m.play(s, {});
  PlayParams lp;
  lp.loop = true;
  const VoiceId loop = m.play(s, lp);
  std::vector<float> out(2 * 9600);
  m.mix(out.data(), 9600);  // 0.2 s
  CHECK(!m.playing(shot));
  CHECK(m.playing(loop));
  CHECK(rms(out) > 0.05f);
  for (float x : out) CHECK(std::fabs(x) <= 1.0f);  // soft-clipped
  m.stop(loop, 0.01f);
  m.mix(out.data(), 2400);
  CHECK(!m.playing(loop));
  CHECK(m.activeVoices() == 0);
}

TEST(mixer_fades_and_buses) {
  Mixer m;
  const SoundId s = m.addSound(tone(1.0f, 220));
  PlayParams p;
  p.loop = true;
  p.fadeIn = 0.5f;
  p.bus = AudioBus::Ambience;
  m.play(s, p);
  std::vector<float> a(2 * 2400), b(2 * 2400);
  m.mix(a.data(), 2400);    // first 50 ms: quiet (fading in)
  for (int i = 0; i < 10; ++i) m.mix(b.data(), 2400);
  CHECK(rms(a) < rms(b));
  m.setBusGain(AudioBus::Ambience, 0.0f, 0.0f);
  m.mix(b.data(), 2400);
  CHECK(rms(b) < 1e-4f);
  CHECK(m.play(0, {}) == kNoVoice);  // invalid sound
}

TEST(audio_content_loads) {
  std::vector<std::string> errors;
  auto manifest = AssetManifest::load(FH_TEST_CONTENT_DIR, &errors);
  CHECK(manifest.has_value());
  if (!manifest) return;
  AudioDirector d;
  CHECK(d.load(*manifest, "audio.config", &errors));
  for (auto& e : errors) std::fprintf(stderr, "    audio: %s\n", e.c_str());
  CHECK(errors.empty());
  AudioState st;
  st.location = "interior.sparrow";
  d.update(st, 0.016f);
  d.event("clamp");
  CHECK(d.mixer().activeVoices() >= 3);  // two beds + clamp
  std::vector<float> out(2 * 4800);
  for (int i = 0; i < 20; ++i) d.mixer().mix(out.data(), 4800);  // 2 s: beds faded in
  CHECK(rms(out) > 0.01f);
}
