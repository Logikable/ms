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

int HeightOf(const ftxui::Element& element) {
  element->ComputeRequirement();
  return element->requirement().min_y;
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
  for (const InspectedTab& tab : tabs_) {
    bundle.push_back(tab.skill);
    every_boosted.insert(every_boosted.end(), tab.boosted.begin(),
                         tab.boosted.end());
  }
  const int budget = split ? columns_ / 2 : columns_;
  const int left_width = bundle.size() > 1 ? WidestCard(bundle, budget) : 0;
  auto lay_left = [&](SkillInspectPanel& card, int tab) {
    const InspectedSkill& skill = tabs_[tab].skill;
    card.SetSkill(skill.skill, skill.learned, skill.bonus);
    card.SetMaxRows(rows_);
    if (bundle.size() > 1) {
      card.SetWidthBounds(left_width, left_width);
      std::vector<TabSpec> names;
      for (const InspectedTab& each : tabs_) {
        names.push_back({each.skill.skill->name()});
      }
      card.SetTabs(std::move(names), tab);
    } else {
      card.SetWidthBounds(0, budget);
      card.SetTabs({}, 0);
    }
  };
  lay_left(own_, own_tab_);
  // Alone, the card has the keys only for its tabs.
  own_.SetFocused(split ? !boosted_focused_ : tabs_.size() > 1);
  ftxui::Element left = own_.Render();
  const int right_width =
      split ? WidestCard(every_boosted, columns_ - WidthOf(left)) : 0;
  auto lay_right = [&](SkillInspectPanel& card, int own_tab, int tab) {
    const std::vector<InspectedSkill>& boosted = tabs_[own_tab].boosted;
    card.SetSkill(boosted[tab].skill, boosted[tab].learned, boosted[tab].bonus);
    card.SetMaxRows(rows_);
    card.SetWidthBounds(right_width, right_width);
    std::vector<TabSpec> names;
    for (const InspectedSkill& each : boosted) {
      names.push_back({each.skill->name()});
    }
    card.SetTabs(std::move(names), tab);
  };

  // Held to the tallest card any tab on either side shows, so turning a tab
  // never moves an edge.
  int height = 0;
  SkillInspectPanel measure;
  for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
    lay_left(measure, i);
    height = std::max(height, HeightOf(measure.Render()));
    for (int j = 0; split && j < static_cast<int>(tabs_[i].boosted.size());
         ++j) {
      lay_right(measure, i, j);
      height = std::max(height, HeightOf(measure.Render()));
    }
  }
  if (!split) {
    return left | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, height);
  }
  ftxui::Element right;
  if (boosted().empty()) {
    right = ThemedWindow(" Skill ", EmptyState("empty")) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, right_width);
  } else {
    lay_right(other_, own_tab_, tab_);
    other_.SetFocused(boosted_focused_);
    right = other_.Render();
  }
  return ftxui::hbox({std::move(left), std::move(right)}) |
         ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, height);
}

}  // namespace ms
