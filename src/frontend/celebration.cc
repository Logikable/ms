#include "src/frontend/celebration.h"

#include <algorithm>

#include "ftxui/dom/elements.hpp"
#include "src/character/progression.h"
#include "src/frontend/panels/advancement_popup_panel.h"
#include "src/frontend/panels/level_up_popup_panel.h"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// Whether a climb from `from` to `to` passed through `level`. Asked of the
// span rather than of the level landed on, so a jump of several levels does
// not step over the one that opened something.
bool CrossedInto(int level, int from, int to) {
  return from < level && level <= to;
}

}  // namespace

void Celebration::BeginLevelUp(int from_level, int to_level, int ap, int sp) {
  kind_ = Kind::kLevelUp;
  remaining_seconds_ = kCelebrationSeconds;
  from_level_ = from_level;
  to_level_ = to_level;
  ap_ = ap;
  sp_ = sp;

  std::fill(std::begin(lit_), std::end(lit_), false);
  // Always the character panel: it is where the AP a level pays out is spent,
  // so it is where the player is being sent every time.
  lit_[kCharPanel] = true;
  // And whichever panels this climb opened. Asked of the unlock table rather
  // than written as levels 3 and 4, because those have moved before and the
  // celebration should follow them.
  if (CrossedInto(UnlockLevel(Feature::kEquipped), from_level, to_level)) {
    lit_[kEquipPanel] = true;
  }
  if (CrossedInto(UnlockLevel(Feature::kBag), from_level, to_level)) {
    lit_[kInventoryPanel] = true;
  }
}

void Celebration::BeginAdvancement(Job from_job, Job to_job) {
  kind_ = Kind::kAdvancement;
  remaining_seconds_ = kCelebrationSeconds;
  from_job_ = from_job;
  to_job_ = to_job;

  std::fill(std::begin(lit_), std::end(lit_), false);
  // The character panel alone: an advancement opens the skills tab, which
  // lives on it, and hands over a job whose stats are read there.
  lit_[kCharPanel] = true;
}

void Celebration::Advance(double elapsed_seconds) {
  if (!active()) {
    return;
  }
  remaining_seconds_ -= elapsed_seconds;
  if (remaining_seconds_ <= 0.0) {
    Dismiss();
  }
}

void Celebration::Dismiss() {
  kind_ = Kind::kNone;
  remaining_seconds_ = 0.0;
  std::fill(std::begin(lit_), std::end(lit_), false);
}

bool Celebration::Lights(Panel panel) const {
  if (panel < 0 || panel >= kNumPanels) {
    return false;
  }
  return lit_[panel];
}

ftxui::Element Celebration::Render() const {
  if (kind_ == Kind::kAdvancement) {
    return AdvancementPopupPanel(from_job_, to_job_);
  }
  return LevelUpPopupPanel(from_level_, to_level_, ap_, sp_);
}

}  // namespace ms
