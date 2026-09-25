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
#include "src/character/honor.h"
#include "src/character/v_matrix.h"
#include "src/combat/loot.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/keys.h"
#include "src/game_state.h"
#include "src/map_force.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/spawn.h"

namespace ms {
namespace {

// Columns of the mob list: the longest mob name, 25 ("Enhanced Diamond
// Guardian"), plus a space.
constexpr int kMobNameWidth = 26;
constexpr int kLevelWidth = 4;
constexpr int kCountWidth = 6;

// The stats column: a label and a right-aligned value, so the panel doesn't
// change width as a number gains a digit. Sized for the longest label and a
// number just below the compact form's threshold, plus a space.
constexpr int kLabelWidth = 7;
constexpr int kValueWidth = 10;

// The drops column, the wider of the two. A drop name can be long enough to
// wrap, which RenderDrops handles; the longest chance is six ("0.025%").
constexpr int kDropNameWidth = 24;
constexpr int kChanceWidth = 6;

// Each column's width including a blank column on each side. The two columns
// and the rule between them fill the width the blurb uses, which is why this
// panel is wider than the others: a sentence needs more room than a stat.
constexpr int kStatColumnWidth = 2 + kLabelWidth + kValueWidth;
constexpr int kDropColumnWidth = 2 + kDropNameWidth + kChanceWidth;
static_assert(kStatColumnWidth + 1 + kDropColumnWidth == kFlavourWidth + 2);

// The indent for the rest of a wrapped drop name, so the two lines read as one
// name.
constexpr int kNameIndent = 2;

// The Arcane River rows span the whole mob list instead of sitting in its
// columns, so they keep their own right gutter. The other rows get one for free
// from a column padded past its text.
constexpr int kTollLabelWidth = kMobNameWidth - 1;

// The screen's fixed height. Tall enough for the mob with the most drops (four
// lines of blurb, a rule, and eight rows of drops inside a border), so moving
// through the list never moves the top of the panel.
constexpr int kScreenHeight = 16;

ftxui::Element InfoRow(const std::string& label, const std::string& value) {
  return ftxui::text(" " + PadRight(label, kLabelWidth) +
                     PadLeft(value, kValueWidth) + " ");
}

ftxui::Element DropRow(const std::string& name, const std::string& chance) {
  return ftxui::text(" " + PadRight(name, kDropNameWidth) +
                     PadLeft(chance, kChanceWidth) + " ");
}

// The share of the character's damage that lands, as a whole percentage. The
// Arcane Force table's steps are all round, so there is never a fraction.
std::string DealtText(double factor) {
  return std::to_string(static_cast<int>(std::llround(factor * 100.0))) + "%";
}

// What the monster's hit is multiplied by. Shown as a multiplier rather than a
// percentage because it goes above one, and "280%" of a hit would read as a
// share of it.
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

// The penalty Arcane River or Grandis applies to fighting what lives there: the
// force the map requires against what the character has, and the two
// multipliers that result. Two rows, or none on a map that requires neither
// force.
void MobInspectPanel::RenderForce(std::vector<ftxui::Element>& rows) const {
  std::map<std::string, MapData>::const_iterator it = state_.maps.find(map_);
  if (it == state_.maps.end() || !AsksForForce(it->second)) {
    return;
  }
  MapForce force = MapForceFor(it->second, state_.character);
  ftxui::Element carried = RedUnless(ftxui::text(std::to_string(force.owned)),
                                     force.owned >= force.required);
  rows.push_back(ftxui::hbox({
      ftxui::text("  " + PadRight(force.name, kTollLabelWidth)),
      std::move(carried),
      ftxui::text(" / " + std::to_string(force.required)),
  }));
  const ForceFactors& factors = force.factors;
  rows.push_back(ftxui::text(
      "  " +
      PadRight("Damage " + DealtText(factors.damage_dealt), kTollLabelWidth) +
      "Taken " + TakenText(factors.damage_taken)));
  rows.push_back(ThemedSeparator());
}

ftxui::Element MobInspectPanel::RenderMobList() const {
  std::vector<ftxui::Element> rows;
  RenderForce(rows);
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
    rows.push_back(HighlightRow(ftxui::text(row), i == selected_));
  }
  std::map<std::string, MapData>::const_iterator it = state_.maps.find(map_);
  std::string title = it == state_.maps.end() ? "Mobs" : it->second.name();
  return ThemedWindow(" " + title + " ", ftxui::vbox(std::move(rows)));
}

void MobInspectPanel::RenderFlavour(std::vector<ftxui::Element>& rows,
                                    const Mob& mob) const {
  std::vector<std::string> lines;
  if (mob.description().empty()) {
    // Some monsters have no description. Saying so is better than four blank
    // rows, which would look like a panel that failed to draw.
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
  // HP in compact form: Arcane River HP runs to eleven digits, which would
  // overflow the column, and a monster's health is read for its size, not its
  // last digit. Anything under a couple of million still shows in full.
  rows.push_back(InfoRow("HP", FormatCompact(mob.max_hp())));
  rows.push_back(InfoRow("EXP", FormatWithCommas(mob.exp())));
  rows.push_back(InfoRow("Attack", FormatWithCommas(mob.attack())));
  // The value of one meso drop, to read alongside its chance in the column
  // beside it. The character's own meso and drop rate are left out, since this
  // panel describes the monster, not the player.
  rows.push_back(InfoRow("Meso", FormatWithCommas(static_cast<int64_t>(
                                     std::llround(MeanMesoPerDrop(mob))))));
  // No Honor row: the honor from a kill is the same from every monster, so it
  // says nothing about this one. Its chance is listed among the drops, where a
  // reward that only sometimes happens belongs.
  return ftxui::vbox(std::move(rows));
}

ftxui::Element MobInspectPanel::RenderDrops(const Mob& mob) const {
  std::vector<ftxui::Element> rows;
  // Meso first, since most kills pay it, and everything below is a chance at a
  // particular item.
  rows.push_back(DropRow("Meso", DropChance(MesoDropChance(0.0))));
  rows.push_back(DropRow("Honor", DropChance(kMobHonorChance)));
  // V Points drop only in Arcane River and Grandis. They are listed here
  // because the drop table is where a player looks to find out where the
  // currency comes from.
  std::map<std::string, MapData>::const_iterator it = state_.maps.find(map_);
  if (it != state_.maps.end() && AsksForForce(it->second)) {
    rows.push_back(DropRow("V Points", DropChance(kVPointDropChance)));
  }
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
      continue;  // a drop nothing would grant
    }
    // Wrapped rather than cut, since half a name means nothing. The chance is
    // on the last line, in the same column as every other chance here.
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
    // Padded to the width a described mob fills, so a map with no mobs doesn't
    // draw a window a fraction of the size of the one beside it.
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
  // The rule between the columns is the same rule that runs across the panel;
  // ftxui draws it vertically inside an hbox.
  rows.push_back(ftxui::hbox({
      RenderStats(mob),
      ThemedSeparator(),
      RenderDrops(mob),
  }));
  return ThemedWindow(" " + mob.name() + " ", ftxui::vbox(std::move(rows)));
}

ftxui::Element MobInspectPanel::Render() const {
  // A fixed height with the panels at the top, so moving through the list
  // (where one mob has more drops than the next) changes only the bottom of the
  // panel and leaves the top where the eye is.
  return ftxui::vbox({
             ftxui::hbox({RenderMobList(), RenderInfo()}),
             ftxui::filler(),
         }) |
         ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, kScreenHeight);
}

}  // namespace ms
