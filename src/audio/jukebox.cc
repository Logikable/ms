#include "src/audio/jukebox.h"

#include <algorithm>
#include <cstddef>
#include <random>
#include <string_view>
#include <utility>
#include <vector>

#include "src/audio/tracks.h"

namespace ms {

Jukebox::Jukebox(std::mt19937& rng) : Jukebox(BgmTrackNames(), rng) {
}

Jukebox::Jukebox(std::vector<std::string_view> tracks, std::mt19937& rng)
    : tracks_(std::move(tracks)), rng_(rng) {
  if (!tracks_.empty()) {
    remembered_ = std::min<int>(kHistory, tracks_.size() - 1);
  }
}

std::string_view Jukebox::Next() {
  if (tracks_.empty()) {
    return {};
  }
  // Drawing until an allowed track turns up would run long on a library only
  // just bigger than the window, so pick out of what is allowed instead.
  std::vector<std::string_view> allowed;
  allowed.reserve(tracks_.size());
  for (std::string_view track : tracks_) {
    if (std::find(recent_.begin(), recent_.end(), track) == recent_.end()) {
      allowed.push_back(track);
    }
  }
  std::uniform_int_distribution<std::size_t> pick(0, allowed.size() - 1);
  std::string_view chosen = allowed[pick(rng_)];
  recent_.push_back(chosen);
  while (recent_.size() > static_cast<std::size_t>(remembered_)) {
    recent_.pop_front();
  }
  return chosen;
}

}  // namespace ms
