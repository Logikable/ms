#include "src/audio/music_player.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include "src/audio/tracks.h"
#include "src/build_config.h"
#include "third_party/miniaudio/miniaudio.h"

namespace ms {

MusicPlayer::MusicPlayer(Backend backend) {
  if (!kAudioEnabled) {
    // No tracks to play, so open no device: a silent build makes no sound
    // thread and touches no sound card.
    return;
  }
  ma_backend null_backend = ma_backend_null;
  ma_context_config context_config = ma_context_config_init();
  ma_result result = ma_context_init(
      backend == Backend::kNull ? &null_backend : nullptr,
      backend == Backend::kNull ? 1 : 0, &context_config, &context_);
  if (result != MA_SUCCESS) {
    return;
  }
  ma_engine_config config = ma_engine_config_init();
  config.pContext = &context_;
  if (ma_engine_init(&config, &engine_) != MA_SUCCESS) {
    ma_context_uninit(&context_);
    return;
  }
  ready_ = true;
  SetVolume(volume_);
}

MusicPlayer::~MusicPlayer() {
  if (!ready_) {
    return;
  }
  for (int slot = 0; slot < 2; ++slot) {
    if (loaded_[slot]) {
      ma_sound_uninit(&sounds_[slot]);
      ma_decoder_uninit(&decoders_[slot]);
    }
  }
  ma_engine_uninit(&engine_);
  ma_context_uninit(&context_);
}

void MusicPlayer::DropFading() {
  int fading = 1 - live_;
  if (!loaded_[fading]) {
    return;
  }
  ma_sound_uninit(&sounds_[fading]);
  ma_decoder_uninit(&decoders_[fading]);
  loaded_[fading] = false;
}

void MusicPlayer::FadeOutLive() {
  if (!loaded_[live_]) {
    return;
  }
  // -1 as the starting volume is miniaudio's "whatever it is now", which is
  // what keeps a fade interrupted halfway from jumping back to full.
  ma_sound_set_fade_in_milliseconds(&sounds_[live_], -1.0f, 0.0f, kFadeMs);
  ma_sound_set_stop_time_in_milliseconds(
      &sounds_[live_], ma_engine_get_time_in_milliseconds(&engine_) + kFadeMs);
  live_ = 1 - live_;
}

void MusicPlayer::Play(std::string_view track) {
  if (!ready_ || (track == playing_ && looping_)) {
    return;
  }
  Start(track, /*looping=*/true);
}

void MusicPlayer::PlayOnce(std::string_view track) {
  if (!ready_) {
    return;
  }
  Start(track, /*looping=*/false);
}

void MusicPlayer::StopLooping() {
  if (ready_ && loaded_[live_]) {
    ma_sound_set_looping(&sounds_[live_], MA_FALSE);
    looping_ = false;
  }
}

bool MusicPlayer::ending() {
  if (!ready_ || !loaded_[live_] || playing_.empty()) {
    return true;
  }
  if (looping_) {
    return false;
  }
  ma_sound& sound = sounds_[live_];
  if (ma_sound_at_end(&sound)) {
    return true;
  }
  // A track whose length the decoder could not tell is left to run out, and
  // ma_sound_at_end above is what catches it -- late, but never early.
  float cursor = 0.0f;
  if (length_seconds_ <= 0.0f ||
      ma_sound_get_cursor_in_seconds(&sound, &cursor) != MA_SUCCESS) {
    return false;
  }
  return length_seconds_ - cursor <= static_cast<float>(kFadeMs) / 1000.0f;
}

void MusicPlayer::Start(std::string_view track, bool looping) {
  std::optional<TrackData> data = BgmTrack(track);
  if (!data.has_value()) {
    // Nothing to play under that name: silence rather than the wrong track.
    Stop();
    return;
  }
  DropFading();
  FadeOutLive();

  ma_decoder_config decoder_config = ma_decoder_config_init_default();
  if (ma_decoder_init_memory(data->data, data->size, &decoder_config,
                             &decoders_[live_]) != MA_SUCCESS) {
    return;
  }
  if (ma_sound_init_from_data_source(&engine_, &decoders_[live_],
                                     MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                                     &sounds_[live_]) != MA_SUCCESS) {
    ma_decoder_uninit(&decoders_[live_]);
    return;
  }
  loaded_[live_] = true;
  // Asked once, here: an MP3's length costs a scan of the file, which is not
  // a price to pay every tick.
  if (ma_sound_get_length_in_seconds(&sounds_[live_], &length_seconds_) !=
      MA_SUCCESS) {
    length_seconds_ = 0.0f;
  }
  ma_sound_set_looping(&sounds_[live_], looping ? MA_TRUE : MA_FALSE);
  looping_ = looping;
  ma_sound_set_fade_in_milliseconds(&sounds_[live_], 0.0f, 1.0f, kFadeMs);
  ma_sound_start(&sounds_[live_]);
  playing_ = std::string(track);
}

void MusicPlayer::Stop() {
  if (!ready_ || playing_.empty()) {
    return;
  }
  DropFading();
  FadeOutLive();
  playing_.clear();
  looping_ = false;
}

void MusicPlayer::SetVolume(int volume) {
  volume_ = std::clamp(volume, 0, 100);
  if (ready_) {
    ma_engine_set_volume(&engine_, static_cast<float>(volume_) / 100.0f);
  }
}

}  // namespace ms
