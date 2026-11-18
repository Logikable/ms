#include "src/frontend/screens/inspect/skill_inspect_screen.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/screens/inspect/skill_inspect_panel.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/text/keys.h"

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

void SkillInspectScreen::SetSkills(std::vector<InspectedTab> tabs) {
  tabs_ = std::move(tabs);
  own_tab_ =
      std::clamp(own_tab_, 0, std::max(0, static_cast<int>(tabs_.size()) - 1));
  tab_ = tabs_.empty()
             ? 0
             : std::clamp(tab_, 0,
                          std::max(0, static_cast<int>(boosted().size()) - 1));
}

void SkillInspectScreen::Reset() {
  own_tab_ = 0;
  tab_ = 0;
  boosted_focused_ = false;
  own_.ResetScroll();
  other_.ResetScroll();
}

bool SkillInspectScreen::Split() const {
  const bool boosts =
      std::any_of(tabs_.begin(), tabs_.end(),
                  [](const InspectedTab& tab) { return !tab.boosted.empty(); });
  return boosts && columns_ / 2 >= kMinSplitCard;
}

bool SkillInspectScreen::OnEvent(const ftxui::Event& event) {
  const bool split = Split();
  if (IsSwitchPanel(event)) {
    boosted_focused_ = split && !boosted_focused_;
    return true;
  }
  const bool right = split && boosted_focused_;
  SkillInspectPanel& card = right ? other_ : own_;
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    card.ScrollBy(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) {
    const int delta = event == ftxui::Event::ArrowLeft ? -1 : 1;
    const int count = right ? static_cast<int>(boosted().size())
                            : static_cast<int>(tabs_.size());
    if (count <= 1) {
      card.ScrollXBy(delta);
      return true;
    }
    if (right) {
      tab_ = StepCursor(tab_, delta, count);
    } else {
      own_tab_ = StepCursor(own_tab_, delta, count);
      own_.ResetScroll();
      // The right card follows, from its first tab.
      tab_ = 0;
    }
    // A new skill, so a new card: from its top.
    other_.ResetScroll();
    return true;
  }
  return false;
}

int SkillInspectScreen::WidestCard(const std::vector<InspectedSkill>& skills,
                                   int budget) {
  SkillInspectPanel measure;
  measure.SetWidthBounds(0, budget);
  int widest = 0;
  for (const InspectedSkill& skill : skills) {
    measure.SetSkill(skill.skill, skill.learned, skill.bonus);
    widest = std::max(widest, WidthOf(measure.Render()));
  }
  return widest;
}

ftxui::Element SkillInspectScreen::Render() const {
  if (tabs_.empty()) {
    return SkillInspectPanel().Render();
  }
  const bool split = Split();
  std::vector<InspectedSkill> bundle;
  std::vector<InspectedSkill> every_boosted;
  std::vector<TabSpec> own_tabs;
  for (const InspectedTab& tab : tabs_) {
    bundle.push_back(tab.skill);
    every_boosted.insert(every_boosted.end(), tab.boosted.begin(),
                         tab.boosted.end());
    own_tabs.push_back({tab.skill.skill->name()});
  }
  const InspectedSkill& own = tabs_[own_tab_].skill;
  own_.SetSkill(own.skill, own.learned, own.bonus);
  own_.SetMaxRows(rows_);
  // Alone, the card has the keys only for its tabs.
  own_.SetFocused(split ? !boosted_focused_ : tabs_.size() > 1);
  if (bundle.size() > 1) {
    const int width = WidestCard(bundle, split ? columns_ / 2 : columns_);
    own_.SetWidthBounds(width, width);
    own_.SetTabs(std::move(own_tabs), own_tab_);
  } else {
    own_.SetWidthBounds(0, split ? columns_ / 2 : columns_);
    own_.SetTabs({}, 0);
  }
  ftxui::Element left = own_.Render();
  if (!split) {
    return left;
  }

  const int width = WidestCard(every_boosted, columns_ - WidthOf(left));
  if (boosted().empty()) {
    return ftxui::hbox(
        {std::move(left),
         ThemedWindow(" Skill ", EmptyState("boosts no other skill")) |
             ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width)});
  }
  std::vector<TabSpec> tabs;
  for (const InspectedSkill& boosted : boosted()) {
    tabs.push_back({boosted.skill->name()});
  }
  const InspectedSkill& shown = boosted()[tab_];
  other_.SetSkill(shown.skill, shown.learned, shown.bonus);
  other_.SetMaxRows(rows_);
  other_.SetWidthBounds(width, width);
  other_.SetTabs(std::move(tabs), tab_);
  other_.SetFocused(boosted_focused_);
  return ftxui::hbox({std::move(left), other_.Render()});
}

}  // namespace ms
