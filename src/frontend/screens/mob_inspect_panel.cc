#include "src/frontend/screens/mob_inspect_panel.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/combat/loot.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/game_state.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/spawn.h"

namespace ms {
namespace {

// Columns of the mob list, the same ones the map select screen sets them in.
constexpr int kMobNameWidth = 22;
constexpr int kLevelWidth = 4;
constexpr int kCountWidth = 6;

// The info rows are a label and a right-aligned value, so the panel does not
// breathe as a number gains a digit. Together they come to the width the blurb
// is set in, which is what makes this panel wider than the rest: a sentence
// needs more room than a stat does.
constexpr int kLabelWidth = 18;
constexpr int kValueWidth = kFlavourWidth - kLabelWidth;

// What the rest of a wrapped drop name is set in from its first line, so the
// two rows read as one name.
constexpr int kNameIndent = 2;

// The rows the screen always takes. Tall enough for the mob carrying the most
// drops, so walking the list never moves the top of the panel.
constexpr int kScreenHeight = 22;

ftxui::Element InfoRow(const std::string& label, const std::string& value) {
  return ftxui::text(" " + PadRight(label, kLabelWidth) +
                     PadLeft(value, kValueWidth) + " ");
}

}  // namespace

MobInspectPanel::MobInspectPanel(const GameState& state) : state_(state) {
}

void MobInspectPanel::SetMap(const std::string& map) {
  map_ = map;
  mobs_.clear();
  selected_ = 0;
  std::map<std::string, MapData>::const_iterator it = state_.maps.find(map);
  if (it == state_.maps.end()) {
    return;
  }
  for (const Spawn& spawn : it->second.spawns()) {
    if (state_.mobs.count(spawn.mob()) > 0) {
      mobs_.push_back({spawn.mob(), SpawnCount(spawn)});
    }
  }
}

void MobInspectPanel::MoveCursor(int delta) {
  selected_ = StepCursor(selected_, delta, static_cast<int>(mobs_.size()));
}

std::string MobInspectPanel::selected_mob() const {
  if (mobs_.empty()) {
    return "";
  }
  return mobs_[selected_].first;
}

ftxui::Element MobInspectPanel::RenderMobList() const {
  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text("  " + PadRight("Name", kMobNameWidth) +
                             PadRight("Lv", kLevelWidth) +
                             PadRight("Count", kCountWidth)));
  rows.push_back(ThemedSeparator());
  if (mobs_.empty()) {
    rows.push_back(EmptyState("empty", 2));
  }
  for (int i = 0; i < static_cast<int>(mobs_.size()); ++i) {
    const Mob& mob = state_.mobs.at(mobs_[i].first);
    std::string row = i == selected_ ? "> " : "  ";
    row += PadRight(mob.name(), kMobNameWidth);
    row += PadRight(std::to_string(mob.level()), kLevelWidth);
    row += PadRight(std::to_string(mobs_[i].second), kCountWidth);
    rows.push_back(ftxui::text(row));
  }
  std::map<std::string, MapData>::const_iterator it = state_.maps.find(map_);
  std::string title = it == state_.maps.end() ? "Mobs" : it->second.name();
  return ThemedWindow(" " + title + " ", ftxui::vbox(std::move(rows)));
}

void MobInspectPanel::RenderFlavour(std::vector<ftxui::Element>& rows,
                                    const Mob& mob) const {
  std::vector<std::string> lines;
  if (mob.description().empty()) {
    // Some monsters the world has written nothing about. Saying so is better
    // than four blank rows, which read as a panel that failed to draw.
    lines.push_back("(no record)");
  } else {
    lines = WrapBalanced(mob.description(), kFlavourWidth);
  }
  for (int i = 0; i < kFlavourLines; ++i) {
    std::string line = i < static_cast<int>(lines.size()) ? lines[i] : "";
    rows.push_back(ftxui::text(" " + PadRight(line, kFlavourWidth) + " "));
  }
}

void MobInspectPanel::RenderStats(std::vector<ftxui::Element>& rows,
                                  const Mob& mob) const {
  rows.push_back(InfoRow("Level", std::to_string(mob.level())));
  rows.push_back(InfoRow("HP", FormatWithCommas(mob.max_hp())));
  rows.push_back(InfoRow("EXP", FormatWithCommas(mob.exp())));
  rows.push_back(InfoRow("Attack", FormatWithCommas(mob.attack())));
  // What one drop is worth, to be read with the chance of one in the list
  // below. The character's own meso and drop rate are left out: this panel
  // describes the monster, not the player standing over it.
  rows.push_back(InfoRow("Meso", FormatWithCommas(static_cast<int64_t>(
                                     std::llround(MeanMesoPerDrop(mob))))));
}

void MobInspectPanel::RenderDrops(std::vector<ftxui::Element>& rows,
                                  const Mob& mob) const {
  // Meso leads: it is the one thing most kills pay, and everything under it
  // is a chance at a particular item.
  rows.push_back(InfoRow("Meso", DropChance(MesoDropChance(0.0))));
  for (const MobDrop& drop : mob.drops()) {
    std::string name;
    if (drop.has_equip()) {
      std::map<std::string, EquipPrototype>::const_iterator it =
          state_.equips.find(drop.equip());
      name = it == state_.equips.end() ? "" : it->second.name();
    } else {
      std::map<std::string, ItemPrototype>::const_iterator it =
          state_.items.find(drop.item());
      name = it == state_.items.end() ? "" : it->second.name();
    }
    if (name.empty()) {
      continue;  // a drop nothing would be granted for
    }
    // Wrapped rather than cut: half a name names nothing. The chance sits on
    // the last line, in the column every other value here stands in.
    std::string chance = DropChance(drop.per_kill());
    std::vector<std::string> lines = WrapBalanced(
        name, kFlavourWidth, static_cast<int>(chance.size()) + 1, kNameIndent);
    for (int i = 0; i + 1 < static_cast<int>(lines.size()); ++i) {
      rows.push_back(
          ftxui::text(" " + PadRight(lines[i], kFlavourWidth) + " "));
    }
    rows.push_back(
        ftxui::text(" " +
                    PadRight(lines.back(),
                             kFlavourWidth - static_cast<int>(chance.size())) +
                    chance + " "));
  }
}

ftxui::Element MobInspectPanel::RenderInfo() const {
  std::string selected = selected_mob();
  if (selected.empty()) {
    // Padded out to the width a described mob fills, so a map standing nothing
    // does not draw a window a fraction of the size of the one beside it.
    return ThemedWindow(" Mob ",
                        ftxui::hbox({
                            EmptyState("empty"),
                            ftxui::text(std::string(kFlavourWidth, ' ')),
                        }));
  }
  const Mob& mob = state_.mobs.at(selected);
  std::vector<ftxui::Element> rows;
  RenderFlavour(rows, mob);
  rows.push_back(ThemedSeparator());
  RenderStats(rows, mob);
  rows.push_back(ThemedSeparator());
  RenderDrops(rows, mob);
  return ThemedWindow(" " + mob.name() + " ", ftxui::vbox(std::move(rows)));
}

ftxui::Element MobInspectPanel::Render() const {
  // Held to a fixed height with the panels at the top of it, so that walking
  // the list -- where one mob has more drops than the next -- moves the bottom
  // of the panel and leaves its top where the eye left it.
  return ftxui::vbox({
             ftxui::hbox({RenderMobList(), RenderInfo()}),
             ftxui::filler(),
         }) |
         ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, kScreenHeight);
}

}  // namespace ms
