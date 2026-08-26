#include "src/frontend/screens/mob_inspect_panel.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/arcane_force.h"
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

// The stats column. Both halves are a label and a right-aligned value, so the
// panel does not breathe as a number gains a digit. "Attack" is the longest
// label the game has, and the widest value is a number just under the compact
// form's threshold ("1,999,999"), so each column carries what it needs and a
// space over.
constexpr int kLabelWidth = 7;
constexpr int kValueWidth = 10;

// The drops column, the wider of the two: the longest drop name is 22 columns
// ("Frozen Secondary Token") and the longest chance six ("0.025%").
constexpr int kDropNameWidth = 24;
constexpr int kChanceWidth = 6;

// What each column comes to with its column of clearance on either side. The
// two of them and the rule between fill the width the blurb is set in, which
// is what makes this panel wider than the rest: a sentence needs more room
// than a stat does.
constexpr int kStatColumnWidth = 2 + kLabelWidth + kValueWidth;
constexpr int kDropColumnWidth = 2 + kDropNameWidth + kChanceWidth;
static_assert(kStatColumnWidth + 1 + kDropColumnWidth == kFlavourWidth + 2);

// What the rest of a wrapped drop name is set in from its first line, so the
// two rows read as one name.
constexpr int kNameIndent = 2;

// The rows the screen always takes. Tall enough for the mob carrying the most
// drops -- four lines of blurb, a rule, and seven rows of drops inside a
// border -- so walking the list never moves the top of the panel.
constexpr int kScreenHeight = 16;

ftxui::Element InfoRow(const std::string& label, const std::string& value) {
  return ftxui::text(" " + PadRight(label, kLabelWidth) +
                     PadLeft(value, kValueWidth) + " ");
}

ftxui::Element DropRow(const std::string& name, const std::string& chance) {
  return ftxui::text(" " + PadRight(name, kDropNameWidth) +
                     PadLeft(chance, kChanceWidth) + " ");
}

// The share of the character's damage that lands, as a whole percentage: the
// Arcane Force table's steps are all round, so there is never a fraction to
// lose.
std::string DealtText(double factor) {
  return std::to_string(static_cast<int>(std::llround(factor * 100.0))) + "%";
}

// What the monster's hit is multiplied by. Written as a multiplier rather than
// a percentage because it goes above one, and "280%" of a hit reads as a share
// of it.
std::string TakenText(double factor) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1fx", factor);
  return buf;
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

// What Arcane River takes for letting the character hurt what lives here: the
// force the map asks for against what they carry, and the two multipliers that
// come of it. Two rows, and none at all outside Arcane River -- every other
// map asks for nothing and takes nothing.
void MobInspectPanel::RenderArcaneForce(
    std::vector<ftxui::Element>& rows) const {
  std::map<std::string, MapData>::const_iterator it = state_.maps.find(map_);
  if (it == state_.maps.end() || it->second.arcane_force() == 0) {
    return;
  }
  int required = it->second.arcane_force();
  int owned = state_.character.arcane_force();
  ftxui::Element carried = ftxui::text(std::to_string(owned));
  if (owned < required) {
    carried = std::move(carried) | ftxui::color(kRed);
  }
  rows.push_back(ftxui::hbox({
      ftxui::text("  " + PadRight("Arcane Force", kMobNameWidth)),
      std::move(carried),
      ftxui::text(" / " + std::to_string(required)),
  }));
  ArcaneFactors factors = ArcaneFactorsFor(owned, required);
  rows.push_back(ftxui::text(
      "  " +
      PadRight("Damage " + DealtText(factors.damage_dealt), kMobNameWidth) +
      "Taken " + TakenText(factors.damage_taken)));
  rows.push_back(ThemedSeparator());
}

ftxui::Element MobInspectPanel::RenderMobList() const {
  std::vector<ftxui::Element> rows;
  RenderArcaneForce(rows);
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

ftxui::Element MobInspectPanel::RenderStats(const Mob& mob) const {
  std::vector<ftxui::Element> rows;
  rows.push_back(InfoRow("Level", std::to_string(mob.level())));
  // HP compactly: Arcane River runs to eleven digits, which would overrun the
  // column, and a monster's health is read for its size rather than its last
  // digit. Anything under a couple of million still reads out in full.
  rows.push_back(InfoRow("HP", FormatCompact(mob.max_hp())));
  rows.push_back(InfoRow("EXP", FormatWithCommas(mob.exp())));
  rows.push_back(InfoRow("Attack", FormatWithCommas(mob.attack())));
  // What one drop is worth, to be read with the chance of one in the column
  // beside it. The character's own meso and drop rate are left out: this panel
  // describes the monster, not the player standing over it.
  rows.push_back(InfoRow("Meso", FormatWithCommas(static_cast<int64_t>(
                                     std::llround(MeanMesoPerDrop(mob))))));
  return ftxui::vbox(std::move(rows));
}

ftxui::Element MobInspectPanel::RenderDrops(const Mob& mob) const {
  std::vector<ftxui::Element> rows;
  // Meso leads: it is the one thing most kills pay, and everything under it
  // is a chance at a particular item.
  rows.push_back(DropRow("Meso", DropChance(MesoDropChance(0.0))));
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
    // the last line, in the column every other chance here stands in.
    std::vector<std::string> lines =
        WrapBalanced(name, kDropNameWidth, /*tail=*/0, kNameIndent);
    for (int i = 0; i + 1 < static_cast<int>(lines.size()); ++i) {
      rows.push_back(DropRow(lines[i], ""));
    }
    rows.push_back(DropRow(lines.back(), DropChance(drop.per_kill())));
  }
  return ftxui::vbox(std::move(rows));
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
  // The rule between the columns is the same one that runs across the panel:
  // ftxui stands it on end inside an hbox.
  rows.push_back(ftxui::hbox({
      RenderStats(mob),
      ThemedSeparator(),
      RenderDrops(mob),
  }));
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
