#include "engine/audio/audio_device.h"

#include <vector>

#include <SDL3/SDL.h>

#include "engine/audio/mixer.h"
#include "engine/core/log.h"

namespace fh {

namespace {
void SDLCALL feed(void* userdata, SDL_AudioStream* stream, int additionalBytes, int /*totalBytes*/) {
  auto* mixer = static_cast<Mixer*>(userdata);
  thread_local std::vector<float> buffer;
  int frames = additionalBytes / int(sizeof(float) * 2);
  while (frames > 0) {
    const int chunk = frames < 1024 ? frames : 1024;
    buffer.resize(size_t(chunk) * 2);
    mixer->mix(buffer.data(), chunk);
    SDL_PutAudioStreamData(stream, buffer.data(), chunk * int(sizeof(float) * 2));
    frames -= chunk;
  }
}
}  // namespace

AudioDevice::~AudioDevice() { close(); }

bool AudioDevice::open(Mixer& mixer) {
  close();
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    log::warn("audio", "no audio subsystem ({}); running silent", SDL_GetError());
    return false;
  }
  sdlInit_ = true;
  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_F32;
  spec.channels = 2;
  spec.freq = kAudioRate;
  mixer_ = &mixer;
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, mixer_);
  if (!stream_) {
    log::warn("audio", "no playback device ({}); running silent", SDL_GetError());
    return false;
  }
  SDL_ResumeAudioStreamDevice(stream_);
  log::info("audio", "playback device open ({} Hz stereo, driver {})", kAudioRate, SDL_GetCurrentAudioDriver());
  return true;
}

void AudioDevice::close() {
  if (stream_) SDL_DestroyAudioStream(stream_);
  stream_ = nullptr;
  if (sdlInit_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
  sdlInit_ = false;
}

}  // namespace fh
