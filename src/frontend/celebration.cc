#include "src/frontend/celebration.h"

#include <algorithm>

#include "ftxui/dom/elements.hpp"
#include "src/character/progression.h"
#include "src/frontend/panels/advancement_popup_panel.h"
#include "src/frontend/panels/death_popup_panel.h"
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

void Celebration::Light(Panel panel, Panel focused) {
  glow_[panel] = panel == focused ? Glow::kTimed : Glow::kUntilVisited;
}

void Celebration::BeginLevelUp(int from_level, int to_level, int ap, int sp,
                               Panel focused) {
  kind_ = Kind::kLevelUp;
  card_seconds_ = kCelebrationSeconds;
  glow_seconds_ = kCelebrationSeconds;
  from_level_ = from_level;
  to_level_ = to_level;
  ap_ = ap;
  sp_ = sp;
  unlocks_.clear();
  for (Feature feature : UpgradesUnlockedBetween(from_level, to_level)) {
    unlocks_.push_back(FeatureName(feature));
  }

  std::fill(std::begin(glow_), std::end(glow_), Glow::kOff);
  // Always the character panel: it is where the AP a level pays out is spent,
  // so it is where the player is being sent every time.
  Light(kCharPanel, focused);
  // And whichever panels this climb opened. Asked of the unlock table rather
  // than written as levels 3 and 4, because those have moved before and the
  // celebration should follow them.
  if (CrossedInto(UnlockLevel(Feature::kEquipped), from_level, to_level)) {
    Light(kEquipPanel, focused);
  }
  if (CrossedInto(UnlockLevel(Feature::kBag), from_level, to_level)) {
    Light(kInventoryPanel, focused);
  }
}

void Celebration::BeginAdvancement(Job from_job, Job to_job, Panel focused) {
  kind_ = Kind::kAdvancement;
  card_seconds_ = kCelebrationSeconds;
  glow_seconds_ = kCelebrationSeconds;
  from_job_ = from_job;
  to_job_ = to_job;

  std::fill(std::begin(glow_), std::end(glow_), Glow::kOff);
  // The character panel alone: an advancement hands over a job whose stats and
  // skills are both read there.
  Light(kCharPanel, focused);
}

void Celebration::BeginDeath() {
  kind_ = Kind::kDeath;
  card_seconds_ = kCelebrationSeconds;
  // glow_ and glow_seconds_ are deliberately left where they are -- see the
  // header. This is the one card that points nowhere, so it takes no panel
  // and gives none back.
}

void Celebration::Advance(double elapsed_seconds) {
  card_seconds_ = std::max(0.0, card_seconds_ - elapsed_seconds);
  glow_seconds_ = std::max(0.0, glow_seconds_ - elapsed_seconds);
  if (glow_seconds_ > 0.0) {
    return;
  }
  // Only the timed ones. A panel still waiting to be visited has not been, and
  // no amount of time passing changes that.
  for (Glow& glow : glow_) {
    if (glow == Glow::kTimed) {
      glow = Glow::kOff;
    }
  }
}

void Celebration::Visit(Panel focused) {
  if (focused < 0 || focused >= kNumPanels) {
    return;
  }
  if (glow_[focused] == Glow::kUntilVisited) {
    glow_[focused] = Glow::kOff;
  }
}

void Celebration::Dismiss() {
  card_seconds_ = 0.0;
}

bool Celebration::Lights(Panel panel) const {
  if (panel < 0 || panel >= kNumPanels) {
    return false;
  }
  return glow_[panel] != Glow::kOff;
}

ftxui::Element Celebration::Render() const {
  if (kind_ == Kind::kDeath) {
    return DeathPopupPanel();
  }
  if (kind_ == Kind::kAdvancement) {
    return AdvancementPopupPanel(from_job_, to_job_);
  }
  return LevelUpPopupPanel(from_level_, to_level_, ap_, sp_, unlocks_);
}

}  // namespace ms
