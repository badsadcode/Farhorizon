#pragma once
// SDL3 playback device that pulls stereo float frames from a Mixer on SDL's audio thread.
// If no device is available (headless CI, no drivers) open() returns false and the game runs
// silently; nothing else changes.
struct SDL_AudioStream;

namespace fh {

class Mixer;

class AudioDevice {
 public:
  AudioDevice() = default;
  ~AudioDevice();
  AudioDevice(const AudioDevice&) = delete;
  AudioDevice& operator=(const AudioDevice&) = delete;

  bool open(Mixer& mixer);
  void close();
  bool isOpen() const { return stream_ != nullptr; }

 private:
  SDL_AudioStream* stream_ = nullptr;
  Mixer* mixer_ = nullptr;
  bool sdlInit_ = false;
};

}  // namespace fh
