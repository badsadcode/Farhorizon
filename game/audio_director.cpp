#include "game/audio_director.h"

#include <algorithm>
#include <cmath>

#include "engine/core/file_io.h"
#include "engine/core/log.h"
#include "game/content/json_util.h"

namespace fh {

namespace {
AudioBus busFromName(const std::string& n) {
  if (n == "ambience") return AudioBus::Ambience;
  if (n == "engine") return AudioBus::Engine;
  if (n == "ui") return AudioBus::Ui;
  return AudioBus::Effects;
}
}  // namespace

float AudioDirector::rand01() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return float(rng_ & 0xFFFFFF) / float(0xFFFFFF);
}

bool AudioDirector::load(const AssetManifest& manifest, const AssetID& configId, std::vector<std::string>* errors) {
  auto path = manifest.resolve(configId, "audio");
  auto text = path ? readTextFile(*path) : std::nullopt;
  content::Reader r("audio '" + configId + "'", errors);
  if (!text) {
    r.error("missing or unreadable");
    return false;
  }
  auto j = nlohmann::json::parse(*text, nullptr, false);
  if (j.is_discarded() || !j.contains("sounds")) {
    r.error("invalid JSON or missing 'sounds'");
    return false;
  }
  for (const auto& [name, sj] : j["sounds"].items()) {
    const std::string file = r.str(sj, "file");
    if (file.find("..") != std::string::npos) {
      r.error("sound '" + name + "': file must stay inside the content root");
      continue;
    }
    std::string err;
    auto buf = loadWav(manifest.contentRoot() / file, &err);
    if (!buf) {
      r.error("sound '" + name + "': " + err);
      continue;
    }
    SoundDef d;
    d.id = mixer_.addSound(std::move(*buf));
    d.bus = busFromName(r.str(sj, "bus", false, "effects"));
    d.gain = float(r.num(sj, "gain", false, 1.0));
    sounds_[name] = d;
  }
  if (j.contains("events"))
    for (const auto& [ev, sn] : j["events"].items()) {
      events_[ev] = sn.get<std::string>();
      if (!sounds_.count(events_[ev])) r.error("event '" + ev + "' uses unknown sound '" + events_[ev] + "'");
    }
  if (j.contains("locations"))
    for (const auto& [key, lj] : j["locations"].items()) {
      LocationDef L;
      if (lj.contains("beds"))
        for (const auto& b : lj["beds"]) L.beds.push_back({r.str(b, "sound"), float(r.num(b, "gain", false, 1.0))});
      if (lj.contains("random")) {
        const auto& rj = lj["random"];
        for (const auto& s : rj["sounds"]) L.random.sounds.push_back(s.get<std::string>());
        L.random.minInterval = float(r.num(rj, "minInterval", false, 10.0));
        L.random.maxInterval = float(r.num(rj, "maxInterval", false, 30.0));
        L.random.gain = float(r.num(rj, "gain", false, 1.0));
      }
      for (const auto& b : L.beds)
        if (!sounds_.count(b.sound)) r.error("location '" + key + "' uses unknown sound '" + b.sound + "'");
      for (const auto& s : L.random.sounds)
        if (!sounds_.count(s)) r.error("location '" + key + "' uses unknown sound '" + s + "'");
      locations_[key] = L;
    }
  baseGain_[0] = float(j.value("ambienceGain", 0.8));
  baseGain_[1] = float(j.value("effectsGain", 0.9));
  baseGain_[2] = float(j.value("engineGain", 0.7));
  baseGain_[3] = float(j.value("uiGain", 0.6));
  applyBusGains();
  log::info("audio", "{} sounds, {} locations, {} events", sounds_.size(), locations_.size(), events_.size());
  return r.ok();
}

void AudioDirector::applyBusGains() {
  mixer_.setBusGain(AudioBus::Ambience, baseGain_[0] * volume_[0], 0.1f);
  mixer_.setBusGain(AudioBus::Effects, baseGain_[1] * volume_[1], 0.1f);
  mixer_.setBusGain(AudioBus::Engine, baseGain_[2] * volume_[2], 0.1f);
  mixer_.setBusGain(AudioBus::Ui, baseGain_[3] * volume_[3], 0.1f);
}

void AudioDirector::setVolumes(float master, float ambience, float effects, float engine, float ui) {
  mixer_.setMasterGain(master);
  volume_[0] = ambience;
  volume_[1] = effects;
  volume_[2] = engine;
  volume_[3] = ui;
  applyBusGains();
}

void AudioDirector::enterLocation(const std::string& key) {
  for (VoiceId v : beds_) mixer_.stop(v, 1.5f);
  beds_.clear();
  current_ = key;
  auto it = locations_.find(key);
  if (it == locations_.end()) return;
  for (const auto& b : it->second.beds) {
    const SoundDef& s = sounds_.at(b.sound);
    PlayParams p;
    p.gain = s.gain * b.gain;
    p.loop = true;
    p.bus = s.bus;
    p.fadeIn = 1.5f;
    p.pan = (rand01() - 0.5f) * 0.4f;
    beds_.push_back(mixer_.play(s.id, p));
  }
  const auto& r = it->second.random;
  nextRandom_ = clock_ + r.minInterval * 0.5f + rand01() * r.minInterval;
}

void AudioDirector::event(const std::string& name, float gain, float pitch, float pan) {
  auto e = events_.find(name);
  if (e == events_.end()) return;
  auto s = sounds_.find(e->second);
  if (s == sounds_.end()) return;
  PlayParams p;
  p.gain = s->second.gain * gain;
  p.pitch = pitch;
  p.pan = pan;
  p.bus = s->second.bus;
  mixer_.play(s->second.id, p);
}

void AudioDirector::update(const AudioState& st, float dt) {
  clock_ += dt;
  if (st.location != current_) enterLocation(st.location);

  // Random hull groans etc.
  auto it = locations_.find(current_);
  if (it != locations_.end() && !it->second.random.sounds.empty() && clock_ >= nextRandom_) {
    const auto& r = it->second.random;
    const auto& name = r.sounds[size_t(rand01() * float(r.sounds.size())) % r.sounds.size()];
    const SoundDef& s = sounds_.at(name);
    PlayParams p;
    p.gain = s.gain * r.gain * (0.6f + 0.4f * rand01());
    p.pitch = 0.8f + 0.35f * rand01();
    p.pan = rand01() * 1.6f - 0.8f;
    p.bus = s.bus;
    mixer_.play(s.id, p);
    nextRandom_ = clock_ + r.minInterval + rand01() * (r.maxInterval - r.minInterval);
  }

  // Engine rumble: only while flying from the cockpit.
  auto engine = sounds_.find("engine_loop");
  const bool flying = st.engineActive;
  if (engine != sounds_.end()) {
    if (flying && engine_ == kNoVoice) {
      PlayParams p;
      p.loop = true;
      p.bus = engine->second.bus;
      p.gain = 0.0f;
      engine_ = mixer_.play(engine->second.id, p);
    }
    if (engine_ != kNoVoice) {
      const float g = flying ? engine->second.gain * (0.18f + 0.82f * st.engineDemand) : 0.0f;
      mixer_.setGain(engine_, g, 0.25f);
      mixer_.setPitch(engine_, 0.75f + 0.25f * st.engineDemand + 0.15f * st.speedFraction);
    }
  }
  auto laser = sounds_.find("laser_loop");
  if (laser != sounds_.end()) {
    if (st.beamActive && laser_ == kNoVoice) {
      PlayParams p;
      p.loop = true;
      p.bus = laser->second.bus;
      p.gain = laser->second.gain;
      p.fadeIn = 0.08f;
      laser_ = mixer_.play(laser->second.id, p);
    } else if (!st.beamActive && laser_ != kNoVoice) {
      mixer_.stop(laser_, 0.15f);
      laser_ = kNoVoice;
    }
  }
}

}  // namespace fh
