#include "src/frontend/screens/skill_inspect_screen.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/screens/skill_inspect_panel.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/keys.h"

namespace ms {
namespace {

// The narrowest a card may be held to beside another. Below it the values
// wrap more than they read, and the single card is the better screen.
constexpr int kMinSplitCard = 50;

int WidthOf(const ftxui::Element& element) {
  element->ComputeRequirement();
  return element->requirement().min_x;
}

}  // namespace

void SkillInspectScreen::SetSkills(InspectedSkill skill,
                                   std::vector<InspectedSkill> boosted) {
  skill_ = skill;
  boosted_ = std::move(boosted);
  tab_ =
      std::clamp(tab_, 0, std::max(0, static_cast<int>(boosted_.size()) - 1));
}

void SkillInspectScreen::Reset() {
  tab_ = 0;
  boosted_focused_ = false;
  own_.ResetScroll();
  other_.ResetScroll();
}

bool SkillInspectScreen::Split() const {
  return !boosted_.empty() && columns_ / 2 >= kMinSplitCard;
}

bool SkillInspectScreen::OnEvent(const ftxui::Event& event) {
  const bool split = Split();
  if (IsSwitchPanel(event)) {
    boosted_focused_ = split && !boosted_focused_;
    return true;
  }
  const bool on_tabs = split && boosted_focused_;
  SkillInspectPanel& card = on_tabs ? other_ : own_;
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    card.ScrollBy(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) {
    const int delta = event == ftxui::Event::ArrowLeft ? -1 : 1;
    if (on_tabs) {
      tab_ = StepCursor(tab_, delta, boosted_.size());
      // A new skill, so a new card: from its top.
      other_.ResetScroll();
    } else {
      card.ScrollXBy(delta);
    }
    return true;
  }
  return false;
}

int SkillInspectScreen::BoostedWidth(int budget) const {
  SkillInspectPanel measure;
  measure.SetWidthBounds(0, budget);
  int widest = 0;
  for (const InspectedSkill& boosted : boosted_) {
    measure.SetSkill(boosted.skill, boosted.learned, boosted.bonus);
    widest = std::max(widest, WidthOf(measure.Render()));
  }
  return widest;
}

ftxui::Element SkillInspectScreen::Render() const {
  own_.SetSkill(skill_.skill, skill_.learned, skill_.bonus);
  own_.SetMaxRows(rows_);
  if (!Split()) {
    own_.SetFocused(false);
    own_.SetWidthBounds(0, columns_);
    return own_.Render();
  }
  own_.SetFocused(!boosted_focused_);
  own_.SetWidthBounds(0, columns_ / 2);
  ftxui::Element left = own_.Render();
  const int width = BoostedWidth(columns_ - WidthOf(left));

  std::vector<TabSpec> tabs;
  for (const InspectedSkill& boosted : boosted_) {
    tabs.push_back({boosted.skill->name()});
  }
  const InspectedSkill& shown = boosted_[tab_];
  other_.SetSkill(shown.skill, shown.learned, shown.bonus);
  other_.SetMaxRows(rows_);
  other_.SetWidthBounds(width, width);
  other_.SetTabs(std::move(tabs), tab_);
  other_.SetFocused(boosted_focused_);
  return ftxui::hbox({std::move(left), other_.Render()});
}

}  // namespace ms
