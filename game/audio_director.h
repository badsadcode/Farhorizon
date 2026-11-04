#pragma once
// Maps game state and events to sounds (design doc 12), entirely from content/audio/audio.json:
//  * ambience beds per location key ("space", interior ids) crossfade on location changes,
//  * random one-shots (hull groans) at irregular intervals per location,
//  * continuous loops driven by state (engine rumble follows thrust, mining laser),
//  * named one-shot events (clamps, airlock, radio chirp, UI beeps, scoop ticks).
// Works without an audio device: the mixer just isn't pulled.
#include <map>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "engine/audio/mixer.h"

namespace fh {

struct AudioState {
  std::string location;   // "space", "interior.sparrow", ...
  float engineDemand = 0; // 0..1 (cockpit, flying)
  float speedFraction = 0;
  bool beamActive = false;
  bool inCockpit = true;
  bool engineActive = false;  // engine rumble audible (flying, travelling, landing)
};

class AudioDirector {
 public:
  bool load(const AssetManifest& manifest, const AssetID& configId, std::vector<std::string>* errors);
  Mixer& mixer() { return mixer_; }
  void update(const AudioState& s, float dt);
  void event(const std::string& name, float gain = 1.0f, float pitch = 1.0f, float pan = 0.0f);
  // Player volumes (Options), multiplied onto the bus gains from audio.json.
  void setVolumes(float master, float ambience, float effects, float engine, float ui);

 private:
  struct SoundDef {
    SoundId id = 0;
    AudioBus bus = AudioBus::Effects;
    float gain = 1.0f;
  };
  struct BedDef {
    std::string sound;
    float gain = 1.0f;
  };
  struct RandomDef {
    std::vector<std::string> sounds;
    float minInterval = 10, maxInterval = 30, gain = 1.0f;
  };
  struct LocationDef {
    std::vector<BedDef> beds;
    RandomDef random;
  };
  float rand01();
  void enterLocation(const std::string& key);

  Mixer mixer_;
  std::map<std::string, SoundDef> sounds_;
  std::map<std::string, LocationDef> locations_;
  std::map<std::string, std::string> events_;  // event name -> sound name
  std::string current_;
  std::vector<VoiceId> beds_;
  double nextRandom_ = 5.0;
  double clock_ = 0;
  VoiceId engine_ = kNoVoice, laser_ = kNoVoice;
  std::uint32_t rng_ = 0x1234567u;
  float baseGain_[4] = {0.8f, 0.9f, 0.7f, 0.6f};  // ambience, effects, engine, ui (from audio.json)
  float volume_[4] = {1, 1, 1, 1};
  void applyBusGains();
};

}  // namespace fh
