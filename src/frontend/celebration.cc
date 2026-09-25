#include "src/frontend/celebration.h"

#include <algorithm>

#include "ftxui/dom/elements.hpp"
#include "src/character/honor.h"
#include "src/character/progression.h"
#include "src/frontend/cards/advancement_card.h"
#include "src/frontend/cards/death_card.h"
#include "src/frontend/cards/level_up_card.h"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// Whether a climb from `from` to `to` passed `level`. It checks the whole
// range, not just the level reached, so a jump of several levels doesn't skip
// the one that unlocked something.
bool CrossedInto(int level, int from, int to) {
  return from < level && level <= to;
}

}  // namespace

void Celebration::Light(Panel panel, Panel focused) {
  glow_[panel] = panel == focused ? Glow::kTimed : Glow::kUntilVisited;
}

void Celebration::BeginLevelUp(int from_level, int to_level, int ap, int sp,
                               int hyper_sp, int account_level, Panel focused) {
  kind_ = Kind::kLevelUp;
  card_seconds_ = kCelebrationSeconds;
  glow_seconds_ = kCelebrationSeconds;
  from_level_ = from_level;
  to_level_ = to_level;
  ap_ = ap;
  sp_ = sp;
  hyper_sp_ = hyper_sp;
  honor_ = HonorVisible(to_level, account_level)
               ? HonorForLevels(from_level, to_level)
               : 0;
  unlocks_.clear();
  for (Feature feature :
       UpgradesUnlockedBetween(from_level, to_level, account_level)) {
    unlocks_.push_back(FeatureName(feature));
  }

  std::fill(std::begin(glow_), std::end(glow_), Glow::kOff);
  // Always the character panel, since that is where the new AP is spent.
  Light(kCharPanel, focused);
  // Also any panels this climb unlocked. It reads the unlock table instead of
  // hard-coding levels, so it follows the table if the levels move. It starts
  // from the account's highest level, so a second character lights nothing.
  int from = std::max(from_level, account_level);
  if (CrossedInto(UnlockLevel(Feature::kEquipped), from, to_level)) {
    Light(kEquipPanel, focused);
  }
  if (CrossedInto(UnlockLevel(Feature::kBag), from, to_level)) {
    Light(kInventoryPanel, focused);
  }
}

void Celebration::BeginAdvancement(Job from_job, Job to_job, int to_stage,
                                   Panel focused) {
  kind_ = Kind::kAdvancement;
  card_seconds_ = kCelebrationSeconds;
  glow_seconds_ = kCelebrationSeconds;
  from_job_ = from_job;
  to_job_ = to_job;
  to_stage_ = to_stage;

  std::fill(std::begin(glow_), std::end(glow_), Glow::kOff);
  // Only the character panel: the new job's stats and skills are both there.
  Light(kCharPanel, focused);
}

void Celebration::BeginDeath() {
  kind_ = Kind::kDeath;
  card_seconds_ = kCelebrationSeconds;
  // glow_ and glow_seconds_ are left alone on purpose; see the header. This
  // card points at no panel, so it neither lights nor clears any.
}

void Celebration::Advance(double elapsed_seconds) {
  card_seconds_ = std::max(0.0, card_seconds_ - elapsed_seconds);
  glow_seconds_ = std::max(0.0, glow_seconds_ - elapsed_seconds);
  if (glow_seconds_ > 0.0) {
    return;
  }
  // Only timed glows. A panel waiting to be visited still hasn't been, however
  // much time passes.
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
    return DeathCard();
  }
  if (kind_ == Kind::kAdvancement) {
    return AdvancementCard(from_job_, to_job_, to_stage_);
  }
  return LevelUpCard(from_level_, to_level_, ap_, sp_, hyper_sp_, honor_,
                     unlocks_);
}

}  // namespace ms
