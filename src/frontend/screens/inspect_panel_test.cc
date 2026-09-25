#include "src/frontend/screens/inspect_panel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/arcane_force.h"
#include "src/character/character.h"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/colors.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

// --- the set card ---

// The Frozen Set as its data file defines it: four pieces named, a weapon and a
// secondary still waiting on items, and the two tiers those four can reach.
std::map<std::string, EquipSet> FrozenSet() {
  const EquipSlot kSlots[] = {EQUIP_SLOT_HAT, EQUIP_SLOT_TOP, EQUIP_SLOT_BOTTOM,
                              EQUIP_SLOT_CAPE};
  const char* kNames[] = {"Frozen Hat", "Frozen Top", "Frozen Bottom",
                          "Frozen Cape"};
  EquipSet set;
  set.set_name(EQUIP_SET_NAME_FROZEN);
  for (int i = 0; i < 4; ++i) {
    EquipSetMember* member = set.add_members();
    member->set_slot(kSlots[i]);
    member->mutable_items()->add_name(kNames[i]);
  }
  EquipSetMember* weapon = set.add_members();
  weapon->set_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  weapon->set_family("Frozen Weapon");
  EquipSetMember* secondary = set.add_members();
  secondary->set_slot(EQUIP_SLOT_SECONDARY);
  secondary->set_family("Frozen Secondary");

  EquipSetTier* three = set.add_tiers();
  three->set_pieces(3);
  SkillEffect* effect = three->mutable_effect();
  effect->set_str(7);
  effect->set_dex(7);
  effect->set_int_(7);
  effect->set_luk(7);
  effect->set_attack(5);
  effect->set_magic_attack(5);
  EquipSetTier* four = set.add_tiers();
  four->set_pieces(4);
  four->mutable_effect()->set_max_hp_pct(0.20);
  four->mutable_effect()->set_max_mp_pct(0.20);
  four->mutable_effect()->set_damage_pct(0.09);
  return {{"frozen", set}};
}

EquipPrototype FrozenPiece(const std::string& name, EquipSlot slot) {
  EquipPrototype proto;
  proto.set_name(name);
  proto.set_equip_slot(slot);
  return proto;
}

// A piece the set names by family rather than by name, as with every Frozen
// weapon and secondary.
EquipPrototype FrozenFamilyPiece(const std::string& name, EquipSlot slot,
                                 const std::string& family) {
  EquipPrototype proto = FrozenPiece(name, slot);
  proto.set_set_family(family);
  return proto;
}

// Wears one piece of the set, so the tiers it reaches light up.
void Wear(CharacterInstance& character, const std::string& name,
          EquipSlot slot) {
  character.PickUp(std::make_unique<EquipInstance>(FrozenPiece(name, slot)));
  character.Equip(static_cast<int>(character.inventory().size()) - 1);
}

void WearFamilyPiece(CharacterInstance& character, const std::string& name,
                     EquipSlot slot, const std::string& family) {
  character.PickUp(
      std::make_unique<EquipInstance>(FrozenFamilyPiece(name, slot, family)));
  character.Equip(static_cast<int>(character.inventory().size()) - 1);
}

class InspectPanelTest : public PanelTest {
 protected:
  static std::string Render(InspectPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, panel.Render());
    return StripAnsi(screen.ToString());
  }

  // A screen with room for an item and the set card beside it, which the 80x20
  // screen clips in both directions.
  static ftxui::Screen Draw(InspectPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                                 ftxui::Dimension::Fixed(34));
    ftxui::Render(screen, panel.Render());
    return screen;
  }

  static std::string RenderWide(InspectPanel& panel) {
    return StripAnsi(Draw(panel).ToString());
  }

  // The width the panel needs when nothing squeezes or stretches it. Not the
  // width it draws at: a window fills whatever box it gets, so a plain render
  // is always as wide as the screen.
  static int NaturalWidth(InspectPanel& panel) {
    ftxui::Element body = panel.Render();
    body->ComputeRequirement();
    return body->requirement().min_x;
  }

  // How many rendered rows contain `text`.
  static int RowsWith(InspectPanel& panel, const std::string& text) {
    std::string rendered = RenderWide(panel);
    int rows = 0;
    size_t start = 0;
    while (start < rendered.size()) {
      size_t end = rendered.find('\n', start);
      if (end == std::string::npos) {
        end = rendered.size();
      }
      if (rendered.substr(start, end - start).find(text) != std::string::npos) {
        ++rows;
      }
      start = end + 1;
    }
    return rows;
  }

  // Whether the first cell of `label` is dimmed. False when the label isn't on
  // screen, so a test checking dimness has to find it first.
  //
  // A row is searched as bytes and read as columns, which differ: a border or a
  // star is one column but three bytes, so the byte where a match starts is
  // nowhere near the column where it is drawn.
  static bool DimAt(InspectPanel& panel, const std::string& label) {
    ftxui::Screen screen = Draw(panel);
    for (int y = 0; y < screen.dimy(); ++y) {
      std::string row;
      std::vector<int> column_of_byte;
      for (int x = 0; x < screen.dimx(); ++x) {
        std::string cell = screen.PixelAt(x, y).character;
        if (cell.empty()) {
          cell = " ";
        }
        row += cell;
        column_of_byte.insert(column_of_byte.end(), cell.size(), x);
      }
      size_t at = row.find(label);
      if (at != std::string::npos) {
        return screen.PixelAt(column_of_byte[at], y).dim;
      }
    }
    return false;
  }

  // Strips ANSI escape sequences so substring searches work regardless of
  // colour.
  static std::string StripAnsi(const std::string& s) {
    std::string out;
    bool in_esc = false;
    for (char c : s) {
      if (c == '\x1b') {
        in_esc = true;
        continue;
      }
      if (in_esc) {
        if (c == 'm') {
          in_esc = false;
        }
        continue;
      }
      out += c;
    }
    return out;
  }

  // The set card as most of these tests open it: a Frozen Hat, on a character
  // who knows the Frozen Set. The fixture owns the item so the panel has
  // something to point at for the whole test.
  InspectPanel& Card() {
    c_.UseEquipSets(FrozenSet());
    card_.UseCharacter(c_);
    card_.SetItem(&hat_);
    return card_;
  }

  EquipInstance hat_{FrozenPiece("Frozen Hat", EQUIP_SLOT_HAT)};
  InspectPanel card_;
};

TEST_F(InspectPanelTest, NullItemShowsPlaceholder) {
  InspectPanel panel;
  EXPECT_NE(Render(panel).find("(no item)"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsTheItemsOwnFields) {
  sword_.set_name("Iron Sword");
  sword_.set_required_level(30);
  sword_.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword_.set_attack_speed(ATTACK_SPEED_AVERAGE);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Iron Sword"), std::string::npos);
  EXPECT_NE(rendered.find("Req Lev: 30"), std::string::npos);
  EXPECT_NE(rendered.find("Warrior"), std::string::npos);
  EXPECT_NE(rendered.find("Type: One-Handed Sword"), std::string::npos);
  EXPECT_NE(rendered.find("Attack Speed: Stage 4 (Average)"),
            std::string::npos);
}

TEST_F(InspectPanelTest, IneligibleJobsStillRendered) {
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Bowman"), std::string::npos);
  EXPECT_NE(rendered.find("Magician"), std::string::npos);
  EXPECT_NE(rendered.find("Pirate"), std::string::npos);
}

TEST_F(InspectPanelTest, UniversalShowsAllJobGroups) {
  sword_.clear_equip_job_categories();
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Beginner"), std::string::npos);
  EXPECT_NE(rendered.find("Warrior"), std::string::npos);
  EXPECT_NE(rendered.find("Bowman"), std::string::npos);
  EXPECT_NE(rendered.find("Magician"), std::string::npos);
  EXPECT_NE(rendered.find("Thief"), std::string::npos);
  EXPECT_NE(rendered.find("Pirate"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsBaseStat) {
  sword_.mutable_base_stats()->set_attack(7);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("+7 "), std::string::npos);
  EXPECT_EQ(Render(panel).find("(7"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsScrollStatBreakdown) {
  sword_.mutable_base_stats()->set_attack(5);
  Equip state;
  state.set_equip_name("Sword");
  state.set_remaining_upgrade_slots(3);
  state.mutable_scroll_stats()->set_attack(3);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("+8 (5 +3)"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsScrollInfo) {
  sword_.set_upgrade_slots(7);
  Equip state;
  state.set_equip_name("Sword");
  state.set_remaining_upgrade_slots(4);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  // 7 slots, 4 left, 0 successes, so 3 were restored.
  EXPECT_NE(Render(panel).find("0 Successful Scrolls"), std::string::npos);
  EXPECT_NE(Render(panel).find("4 Left, 3 Restores"), std::string::npos);
}

// A Legendary potential on a weapon whose middle line rolled one rank lower,
// which is why per-line colours are worth drawing.
Potential WeaponPotential() {
  Potential potential;
  potential.set_rank(POTENTIAL_RANK_LEGENDARY);
  PotentialLine* first = potential.add_lines();
  first->set_type(POTENTIAL_LINE_TYPE_LUK_PCT);
  first->set_rank(POTENTIAL_RANK_LEGENDARY);
  PotentialLine* second = potential.add_lines();
  second->set_type(POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30);
  second->set_rank(POTENTIAL_RANK_UNIQUE);
  PotentialLine* third = potential.add_lines();
  third->set_type(POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_35);
  third->set_rank(POTENTIAL_RANK_LEGENDARY);
  return potential;
}

TEST_F(InspectPanelTest, ShowsPotentialUnderTheScrollCount) {
  sword_.set_required_level(100);
  sword_.set_upgrade_slots(7);
  Equip state;
  *state.mutable_main_potential() = WeaponPotential();
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Legendary Potential"), std::string::npos)
      << rendered;
  // Each line's value depends on the item's level, so a level 100 weapon gets
  // the third band.
  EXPECT_NE(rendered.find("◼  LUK  +12%"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("◼  Boss Damage  +30%"), std::string::npos)
      << rendered;
  EXPECT_NE(rendered.find("◼  Ignore DEF  +35%"), std::string::npos)
      << rendered;
  EXPECT_LT(rendered.find("Successful Scroll"),
            rendered.find("Legendary Potential"));
}

TEST_F(InspectPanelTest, PaintsEveryPotentialLineItsOwnRank) {
  sword_.set_required_level(100);
  Equip state;
  *state.mutable_main_potential() = WeaponPotential();
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  ftxui::Screen screen = Draw(panel);
  EXPECT_EQ(ColorOf(screen, "Legendary Potential"), kLegendary.ToColor());
  // The dot shows the line's own rank, which the middle one doesn't share with
  // the header.
  EXPECT_EQ(ColorOf(screen, "◼  LUK"), kLegendary.ToColor());
  EXPECT_EQ(ColorOf(screen, "◼  Boss Damage"), kUnique.ToColor());
  // The line's text stays plain; the dot shows the rank.
  EXPECT_NE(ColorOf(screen, "Boss Damage"), kUnique.ToColor());
}

TEST_F(InspectPanelTest, AnItemWithNoPotentialSaysNothingAboutIt) {
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_EQ(RenderWide(panel).find("Potential"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsPercentageStatsUnderTheFlatOnes) {
  sword_.mutable_base_stats()->set_attack(7);
  sword_.mutable_base_stats()->set_max_hp_pct(10);
  sword_.mutable_base_stats()->set_boss_damage(30);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Max HP  +10%"), std::string::npos);
  EXPECT_NE(rendered.find("Boss Damage  +30%"), std::string::npos);
  EXPECT_LT(rendered.find("+7 "), rendered.find("Max HP  +10%"));
  EXPECT_EQ(rendered.find("Max MP"), std::string::npos);
}

// A stat at zero has no row, and an item with no stats says so instead of
// showing an empty column.
TEST_F(InspectPanelTest, AnItemWithNoStatsSaysSoRatherThanShowingZeroes) {
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_EQ(rendered.find("DEF"), std::string::npos);
  EXPECT_NE(rendered.find("(no stats)"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsStarForceStatBreakdown) {
  sword_.mutable_base_stats()->set_attack(7);
  sword_.set_required_level(10);
  Equip state;
  state.set_equip_name("Sword");
  state.set_stars(5);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  // 5 stars on a level-10 warrior weapon: star force gives STR 10, DEX 10, ATT
  // 5. STR: base 0, star force 10, so "+10 (0 +10)". ATT: base 7, star force 5,
  // so "+12 (7 +5)".
  EXPECT_NE(Render(panel).find("+10 (0 +10)"), std::string::npos);
  EXPECT_NE(Render(panel).find("+12 (7 +5)"), std::string::npos);
}

TEST_F(InspectPanelTest, StarBarShowsFilledAndEmptyStars) {
  // A level 0 item (max 5 stars) at 3 stars: 3 filled and 2 empty.
  Equip state;
  state.set_stars(3);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("★★★☆☆"), std::string::npos);
}

TEST_F(InspectPanelTest, StarBarLengthReflectsItemMaxStars) {
  // A level 95 item has max 8 stars, so the bar is split into groups of 5 and
  // 3.
  sword_.set_required_level(95);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  // An empty 8-star bar: "☆☆☆☆☆ ☆☆☆" (5, a space, then 3).
  EXPECT_NE(Render(panel).find("☆☆☆☆☆"), std::string::npos);
}

TEST_F(InspectPanelTest, AnItemThatRefusesStarForceHasNoBar) {
  sword_.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_EQ(rendered.find("☆"), std::string::npos);
  EXPECT_EQ(rendered.find("★"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsTraceNameWithSuffix) {
  sword_.set_name("Iron Sword");
  Equip state;
  state.set_equip_name("Iron Sword");
  EquipTrace trace(sword_, state);
  InspectPanel panel;
  panel.SetItem(&trace);
  EXPECT_NE(Render(panel).find("Iron Sword Trace"), std::string::npos);
}

// --- stackable items ---

// The same screen, reached the same way, for an item with a sentence instead of
// stats.
ItemPrototype MakeStackable(const std::string& name,
                            const std::string& description) {
  ItemPrototype item;
  item.set_name(name);
  item.set_description(description);
  return item;
}

TEST_F(InspectPanelTest, ShowsAStackablesNameAndDescription) {
  ItemPrototype item =
      MakeStackable("Green Snail Shell", "A shell shed by a snail.");
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Green Snail Shell"), std::string::npos);
  EXPECT_NE(rendered.find("A shell shed by a snail."), std::string::npos);
}

// A description longer than the window wraps instead of running off the edge or
// stretching the window.
TEST_F(InspectPanelTest, WrapsALongDescription) {
  ItemPrototype item = MakeStackable(
      "Elixir",
      "A thick green draught that restores every point of health and magic "
      "the drinker has spent, and tastes of pine needles besides.");
  InspectPanel panel;
  panel.SetItem(&item);
  ftxui::Element element = panel.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element),
                                               ftxui::Dimension::Fit(element));
  ftxui::Render(screen, element);
  EXPECT_LE(screen.dimx(), 44);
  EXPECT_GT(screen.dimy(), 5) << "the description took more than one row";
}

// Every description is shown at the same width, so the window doesn't resize as
// the cursor moves between items.
TEST_F(InspectPanelTest, EveryStackableIsTheSameWidth) {
  ItemPrototype terse = MakeStackable("Pill", "Small.");
  ItemPrototype wordy = MakeStackable(
      "Elixir", "A thick green draught that restores every point of health.");
  InspectPanel panel;
  panel.SetItem(&terse);
  ftxui::Element narrow = panel.Render();
  panel.SetItem(&wordy);
  ftxui::Element wide = panel.Render();
  EXPECT_EQ(ftxui::Screen::Create(ftxui::Dimension::Fit(narrow)).dimx(),
            ftxui::Screen::Create(ftxui::Dimension::Fit(wide)).dimx());
}

TEST_F(InspectPanelTest, SaysSoWhenAStackableHasNoDescription) {
  ItemPrototype item = MakeStackable("Green Snail Shell", "");
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Green Snail Shell"), std::string::npos);
  EXPECT_NE(rendered.find("(no description)"), std::string::npos);
}

// The two kinds are exclusive: the panel describes the item the cursor was last
// on, not both.
TEST_F(InspectPanelTest, EitherKindOfItemReplacesTheOther) {
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  EquipInstance equip(proto);
  ItemPrototype potion =
      MakeStackable("Green Snail Shell", "A shell shed by a snail.");

  InspectPanel panel;
  panel.SetItem(&equip);
  panel.SetItem(&potion);
  EXPECT_EQ(Render(panel).find("Sword"), std::string::npos);

  panel.SetItem(&equip);
  std::string back = Render(panel);
  EXPECT_NE(back.find("Sword"), std::string::npos);
  EXPECT_EQ(back.find("Recovers 50 HP."), std::string::npos);
}

// --- a narrow item gets a narrow card ---

// The six job categories are the same on every item, and the star bar is as
// long as the item's level allows. Neither should decide how wide the card is.
TEST_F(InspectPanelTest, FoldsTheJobRowWhenNothingElseIsWide) {
  sword_.clear_equip_job_categories();
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Beginner / Warrior / Bowman"), std::string::npos);
  EXPECT_NE(rendered.find("Magician / Thief / Pirate"), std::string::npos);
  EXPECT_EQ(rendered.find("Bowman / Magician"), std::string::npos);
  EXPECT_LT(NaturalWidth(panel), 35);
}

TEST_F(InspectPanelTest, KeepsTheJobRowWholeWhenTheCardIsWideAnyway) {
  sword_.set_name(
      "Fafnir Windwing Shooter of Preposterous Length and Renown Trace");
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(RenderWide(panel).find("Bowman / Magician"), std::string::npos);
}

TEST_F(InspectPanelTest, FoldsAStarBarPastFifteen) {
  sword_.set_required_level(150);  // 30 stars
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_EQ(RowsWith(panel, "☆"), 2);
  // Fifteen per row, grouped in fives from the start of each row.
  EXPECT_EQ(RowsWith(panel, "☆☆☆☆☆ ☆☆☆☆☆ ☆☆☆☆☆"), 2);
  EXPECT_LT(NaturalWidth(panel), 35);
}

// The remainder gets its own row however short it is, centred under the first.
TEST_F(InspectPanelTest, FoldsAStarBarIntoFifteenAndTheRest) {
  sword_.set_required_level(128);  // 20 stars
  Equip state;
  state.set_stars(17);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_EQ(RowsWith(panel, "★★★★★ ★★★★★ ★★★★★"), 1);
  EXPECT_EQ(RowsWith(panel, "★★☆☆☆"), 1);
}

TEST_F(InspectPanelTest, KeepsAStarBarOfFifteenOnOneRow) {
  sword_.set_required_level(118);  // 15 stars
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_EQ(RowsWith(panel, "☆"), 1);
}

TEST_F(InspectPanelTest, NoSetCardForAnItemInNoSet) {
  EquipInstance sword(sword_);
  c_.UseEquipSets(FrozenSet());

  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetItem(&sword);
  EXPECT_EQ(RenderWide(panel).find("Set Effect"), std::string::npos);
}

// The panel can be used without a character (in a test, or a screen that shows
// one item), and then it knows of no sets.
TEST_F(InspectPanelTest, NoSetCardWithoutACharacter) {
  InspectPanel panel;
  panel.SetItem(&hat_);
  EXPECT_EQ(RenderWide(panel).find("Set Effect"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsTheWholeSetBesideOneOfItsPieces) {
  InspectPanel& panel = Card();
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Set Effect"), std::string::npos);
  EXPECT_NE(rendered.find("Frozen Set"), std::string::npos);
  EXPECT_NE(rendered.find("Hat        Frozen Hat"), std::string::npos);
  EXPECT_NE(rendered.find("Cape       Frozen Cape"), std::string::npos);
  // The two slots with no item defined yet name what they are waiting for.
  EXPECT_NE(rendered.find("Weapon     Choose 1 Frozen Weapon"),
            std::string::npos);
  EXPECT_NE(rendered.find("Secondary  Choose 1 Frozen Secondary"),
            std::string::npos);
}

// The scroll screen puts its list where the set card would go, and three
// windows in a row would leave none of them enough width. A screen showing the
// same item twice titles its cards itself, as Star Force does with Before and
// After.
TEST_F(InspectPanelTest, TheCardAloneLeavesTheSetCardOut) {
  InspectPanel& panel = Card();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                               ftxui::Dimension::Fixed(34));
  ftxui::Render(screen, panel.RenderItemOnly());
  std::string rendered = StripAnsi(screen.ToString());
  EXPECT_EQ(rendered.find("Set Effect"), std::string::npos);
  EXPECT_NE(rendered.find("Frozen Hat"), std::string::npos);
  EXPECT_NE(rendered.find("Inspect"), std::string::npos);

  ftxui::Render(screen, panel.RenderItemOnly(/*focused=*/false, " Before "));
  rendered = StripAnsi(screen.ToString());
  EXPECT_NE(rendered.find("Before"), std::string::npos);
  EXPECT_EQ(rendered.find("Inspect"), std::string::npos);
}

TEST_F(InspectPanelTest, ReadsWhatEachTierPays) {
  InspectPanel& panel = Card();
  std::string rendered = RenderWide(panel);
  // Four equal stats show as one row, and so do pairs that come together. Only
  // the first line of a tier has its label.
  EXPECT_NE(rendered.find("3 Set Effect   All Stats +7"), std::string::npos);
  EXPECT_NE(rendered.find("                Attack Power & Magic ATT +5"),
            std::string::npos);
  EXPECT_NE(rendered.find("4 Set Effect   Max HP & MP +20%"),
            std::string::npos);
  EXPECT_NE(rendered.find("                Damage +9%"), std::string::npos);
}

TEST_F(InspectPanelTest, SplitsAPairWhoseHalvesDisagree) {
  std::map<std::string, EquipSet> sets = FrozenSet();
  SkillEffect* three = sets["frozen"].mutable_tiers(0)->mutable_effect();
  three->set_magic_attack(3);
  three->set_luk(4);
  c_.UseEquipSets(sets);

  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetItem(&hat_);
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Attack Power +5"), std::string::npos);
  EXPECT_NE(rendered.find("Magic ATT +3"), std::string::npos);
  EXPECT_EQ(rendered.find("Attack Power & Magic ATT"), std::string::npos);
  // Stats that differ get four rows of their own.
  EXPECT_EQ(rendered.find("All Stats"), std::string::npos);
  EXPECT_NE(rendered.find("STR +7"), std::string::npos);
  EXPECT_NE(rendered.find("LUK +4"), std::string::npos);
}

TEST_F(InspectPanelTest, DimsThePiecesNotBeingWorn) {
  InspectPanel& panel = Card();
  EXPECT_TRUE(DimAt(panel, "Hat        Frozen Hat"));
  EXPECT_TRUE(DimAt(panel, "Cape       Frozen Cape"));
  // Nothing of that family is on the character, so the slot is still asking for
  // one.
  EXPECT_TRUE(DimAt(panel, "Weapon     Choose 1 Frozen Weapon"));

  // The inspected item doesn't count; only what is on the character does.
  Wear(c_, "Frozen Hat", EQUIP_SLOT_HAT);
  EXPECT_FALSE(DimAt(panel, "Hat        Frozen Hat"));
  EXPECT_TRUE(DimAt(panel, "Cape       Frozen Cape"));
}

// A slot is filled by whichever of its alternates is worn, not only the first
// one listed, and a slot family wide enough to hold two lights both.
TEST_F(InspectPanelTest, EveryWornAlternateOfASlotIsLit) {
  EquipSet set;
  set.set_name(EQUIP_SET_NAME_BOSS_ACCESSORY);
  EquipSetMember* member = set.add_members();
  member->set_slot(EQUIP_SLOT_RING);
  for (const char* name : {"Ring A", "Ring B", "Ring C"}) {
    member->mutable_items()->add_name(name);
  }
  // Inspected from another slot of the same set, so the ring names appear only
  // on the set card.
  set.add_members()->set_slot(EQUIP_SLOT_BELT);
  set.mutable_members(1)->mutable_items()->add_name("Belt");
  c_.UseEquipSets({{"rings", set}});
  EquipInstance belt(FrozenPiece("Belt", EQUIP_SLOT_BELT));
  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetItem(&belt);
  EXPECT_TRUE(DimAt(panel, "Ring       Ring A"));
  EXPECT_TRUE(DimAt(panel, "Ring B"));

  // The second alternate fills the slot, so the slot reads as filled even
  // though the piece above it isn't the one filling it.
  Wear(c_, "Ring B", EQUIP_SLOT_RING);
  EXPECT_FALSE(DimAt(panel, "Ring       Ring A"));
  EXPECT_TRUE(DimAt(panel, "Ring A"));
  EXPECT_FALSE(DimAt(panel, "Ring B"));

  // A second ring goes in its own ring slot and lights separately.
  Wear(c_, "Ring C", EQUIP_SLOT_RING);
  EXPECT_FALSE(DimAt(panel, "Ring C"));
  EXPECT_TRUE(DimAt(panel, "Ring A"));
}

// A slot can name specific pieces and also accept a family: the Boss Accessory
// shoulder slot takes one Magnus drop or any of the four Cygnus shoulders.
TEST_F(InspectPanelTest, ASlotNamesItsOwnPiecesAndItsFamily) {
  EquipSet set;
  set.set_name(EQUIP_SET_NAME_BOSS_ACCESSORY);
  EquipSetMember* member = set.add_members();
  member->set_slot(EQUIP_SLOT_SHOULDER);
  member->mutable_items()->add_name("Plain Shoulder");
  member->set_family("Cygnus Shoulder");
  c_.UseEquipSets({{"boss", set}});

  EquipInstance shoulder(FrozenPiece("Plain Shoulder", EQUIP_SLOT_SHOULDER));
  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetItem(&shoulder);
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Shoulder   Plain Shoulder"), std::string::npos);
  EXPECT_NE(rendered.find("Choose 1 Cygnus Shoulder"), std::string::npos);
  EXPECT_TRUE(DimAt(panel, "Shoulder   Plain Shoulder"));

  // The family piece counts, and the slot is filled once, whichever way.
  WearFamilyPiece(c_, "Lionheart Battle Shoulder", EQUIP_SLOT_SHOULDER,
                  "Cygnus Shoulder");
  rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Lionheart Battle Shoulder"), std::string::npos);
  EXPECT_EQ(rendered.find("Choose 1"), std::string::npos);
  EXPECT_FALSE(DimAt(panel, "Shoulder   Plain Shoulder"));
  EXPECT_EQ(c_.PiecesWornOf(set), 1);
}

// A family slot asks for a piece until one is worn, then names the piece
// wearing it. That is also when the tiers past four become reachable.
TEST_F(InspectPanelTest, AWornFamilyPieceNamesItselfInItsSlot) {
  InspectPanel& panel = Card();
  ASSERT_NE(RenderWide(panel).find("Weapon     Choose 1 Frozen Weapon"),
            std::string::npos);

  WearFamilyPiece(c_, "Frozen Polearm", EQUIP_SLOT_PRIMARY_WEAPON,
                  "Frozen Weapon");
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Weapon     Frozen Polearm"), std::string::npos);
  EXPECT_EQ(rendered.find("Choose 1 Frozen Weapon"), std::string::npos);
  EXPECT_FALSE(DimAt(panel, "Weapon     Frozen Polearm"));
}

// The set names no weapon, so a weapon has to match by family or the card would
// never open beside one.
TEST_F(InspectPanelTest, AFamilyPieceOpensTheSameCard) {
  EquipPrototype proto = FrozenFamilyPiece(
      "Frozen Polearm", EQUIP_SLOT_PRIMARY_WEAPON, "Frozen Weapon");
  EquipInstance polearm(proto);
  c_.UseEquipSets(FrozenSet());

  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetItem(&polearm);
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Frozen Set"), std::string::npos);
  EXPECT_NE(rendered.find("Hat        Frozen Hat"), std::string::npos);
}

TEST_F(InspectPanelTest, DimsTheTiersTheCharacterHasNotEarned) {
  InspectPanel& panel = Card();
  EXPECT_TRUE(DimAt(panel, "3 Set Effect"));
  EXPECT_TRUE(DimAt(panel, "4 Set Effect"));

  Wear(c_, "Frozen Hat", EQUIP_SLOT_HAT);
  Wear(c_, "Frozen Top", EQUIP_SLOT_TOP);
  Wear(c_, "Frozen Bottom", EQUIP_SLOT_BOTTOM);
  EXPECT_FALSE(DimAt(panel, "3 Set Effect"));
  // Every line of the tier lights up, not only the labelled one.
  EXPECT_FALSE(DimAt(panel, "Attack Power & Magic ATT +5"));
  EXPECT_TRUE(DimAt(panel, "4 Set Effect"));
}

// The card sits beside the item, so a card that resized with its contents would
// shift the item panel across the screen.
TEST_F(InspectPanelTest, TheCardIsOneWidthWhateverTheSetHolds) {
  InspectPanel& panel = Card();
  int wide = NaturalWidth(panel);

  std::map<std::string, EquipSet> small = FrozenSet();
  SkillEffect* three = small["frozen"].mutable_tiers(0)->mutable_effect();
  three->Clear();
  three->set_str(1);
  c_.UseEquipSets(small);

  EXPECT_EQ(NaturalWidth(panel), wide);
}

// --- scrolling ---

// A set with enough tiers to outgrow any terminal, so the card has something to
// scroll.
std::map<std::string, EquipSet> TallSet() {
  std::map<std::string, EquipSet> sets = FrozenSet();
  EquipSet& set = sets["frozen"];
  for (int i = 5; i < 40; ++i) {
    EquipSetTier* tier = set.add_tiers();
    tier->set_pieces(i);
    tier->mutable_effect()->set_str(i);
  }
  return sets;
}

// A panel on a Frozen Hat with a set card beside it, both cut to `rows`.
InspectPanel TallPanel(CharacterInstance& character, int rows) {
  character.UseEquipSets(TallSet());
  InspectPanel panel;
  panel.UseCharacter(character);
  panel.SetMaxRows(rows);
  return panel;
}

// Only the tiers move: the set's name and pieces are what the tiers are read
// against, so they stay put.
TEST_F(InspectPanelTest, ScrollsTheSetCardWithoutMovingTheItemCard) {
  InspectPanel panel = TallPanel(c_, 18);
  panel.SetItem(&hat_);
  ASSERT_NE(RenderWide(panel).find("3 Set Effect"), std::string::npos);

  ASSERT_TRUE(panel.SwapCard(1));
  EXPECT_EQ(panel.focused_card(), InspectPanel::kSetCard);
  panel.ScrollBy(3);
  std::string rendered = RenderWide(panel);
  EXPECT_EQ(rendered.find("3 Set Effect"), std::string::npos)
      << "the tiers have moved";
  EXPECT_NE(rendered.find("Frozen Set"), std::string::npos)
      << "the head has not";
  EXPECT_NE(rendered.find("Req Lev"), std::string::npos)
      << "and neither has the item card";
}

// The stats are the only part of the item card that moves.
TEST_F(InspectPanelTest, ScrollsTheItemCardBetweenItsHeadAndItsFoot) {
  sword_.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword_.set_upgrade_slots(7);
  EquipStats* stats = sword_.mutable_base_stats();
  stats->set_str(5);
  stats->set_dex(5);
  stats->set_int_(5);
  stats->set_luk(5);
  stats->set_max_hp(5);
  stats->set_max_mp(5);
  stats->set_attack(5);
  Equip state;
  state.set_equip_name("Sword");
  state.set_remaining_upgrade_slots(7);
  EquipInstance sword(sword_, state);
  InspectPanel panel;
  // Room for the top, the bottom and two lines of stats between.
  panel.SetMaxRows(14);
  panel.SetItem(&sword);
  ASSERT_NE(RenderWide(panel).find("Type:"), std::string::npos);

  panel.ScrollBy(2);
  std::string rendered = RenderWide(panel);
  EXPECT_EQ(rendered.find("Type:"), std::string::npos) << "the stats moved";
  EXPECT_NE(rendered.find("Req Lev"), std::string::npos) << "the head did not";
  EXPECT_NE(rendered.find("Successful Scroll"), std::string::npos)
      << "and neither did the foot";
}

TEST_F(InspectPanelTest, ResetPutsBothCardsBackAtTheTop) {
  InspectPanel panel = TallPanel(c_, 12);
  panel.SetItem(&hat_);
  RenderWide(panel);
  ASSERT_TRUE(panel.SwapCard(1));
  panel.ScrollBy(5);
  RenderWide(panel);

  panel.Reset();
  EXPECT_EQ(panel.focused_card(), InspectPanel::kItemCard);
  EXPECT_NE(RenderWide(panel).find("Frozen Set"), std::string::npos);
}

// Two cards form a ring of two: the switch key returns to the first card
// instead of stopping on the second.
TEST_F(InspectPanelTest, TabCyclesBackToTheItemCard) {
  InspectPanel panel = TallPanel(c_, 18);
  panel.SetItem(&hat_);
  RenderWide(panel);

  ASSERT_TRUE(panel.SwapCard(1));
  ASSERT_EQ(panel.focused_card(), InspectPanel::kSetCard);
  EXPECT_TRUE(panel.SwapCard(1));
  EXPECT_EQ(panel.focused_card(), InspectPanel::kItemCard);
}

// A card that fits is still a stop, since skipping it would leave the arrows
// stuck on the other one.
TEST_F(InspectPanelTest, TabReachesACardWithNothingToScroll) {
  c_.UseEquipSets(FrozenSet());
  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetMaxRows(34);
  panel.SetItem(&hat_);
  RenderWide(panel);
  EXPECT_TRUE(panel.SwapCard(1)) << "the whole set fits, and is still a stop";
  EXPECT_EQ(panel.focused_card(), InspectPanel::kSetCard);
}

TEST_F(InspectPanelTest, NoTabWithoutASetCard) {
  EquipInstance sword(sword_);
  c_.UseEquipSets(FrozenSet());
  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetMaxRows(6);
  panel.SetItem(&sword);
  RenderWide(panel);
  EXPECT_FALSE(panel.HasSetCard());
  EXPECT_FALSE(panel.SwapCard(1));
}

// --- the equipped card ---

// A second hat to show beside the inspected one, with a different name so a
// rendered screen shows which card is which.
EquipInstance WornHat() {
  return EquipInstance(FrozenPiece("Old Hat", EQUIP_SLOT_HAT));
}

TEST_F(InspectPanelTest, DrawsTheEquippedItemToTheLeftOfTheInspectedOne) {
  EquipInstance worn = WornHat();
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&hat_);
  panel.SetComparison(&worn);
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Equipped"), std::string::npos);
  EXPECT_LT(rendered.find("Old Hat"), rendered.find("Frozen Hat"))
      << "the worn item first, the inspected one beside it";
}

// The set belongs to the inspected item and is counted from the character. A
// second copy beside the equipped card would repeat it.
TEST_F(InspectPanelTest, TheEquippedCardCarriesNoSetCard) {
  EquipInstance worn = WornHat();
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&hat_);
  panel.SetComparison(&worn);
  std::string rendered = RenderWide(panel);
  size_t first = rendered.find("Frozen Set");
  EXPECT_NE(first, std::string::npos);
  EXPECT_EQ(rendered.find("Frozen Set", first + 1), std::string::npos)
      << "one set card on the screen, not one per item";
}

TEST_F(InspectPanelTest, SettingAnItemForgetsTheComparison) {
  EquipInstance worn = WornHat();
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&hat_);
  panel.SetComparison(&worn);
  ASSERT_NE(RenderWide(panel).find("Equipped"), std::string::npos);

  panel.SetItem(&hat_);
  EXPECT_EQ(RenderWide(panel).find("Equipped"), std::string::npos);
}

// The ring follows the drawing order, and it starts on the middle card: the
// item the player asked about, not the one they already have.
TEST_F(InspectPanelTest, TabWalksTheThreeCardsInTheOrderTheyAreDrawn) {
  EquipInstance worn = WornHat();
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&hat_);
  panel.SetComparison(&worn);
  RenderWide(panel);

  EXPECT_EQ(panel.focused_card(), InspectPanel::kItemCard);
  ASSERT_TRUE(panel.SwapCard(1));
  EXPECT_EQ(panel.focused_card(), InspectPanel::kSetCard);
  panel.SwapCard(1);
  EXPECT_EQ(panel.focused_card(), InspectPanel::kEquippedCard);
  panel.SwapCard(1);
  EXPECT_EQ(panel.focused_card(), InspectPanel::kItemCard);
  panel.SwapCard(-1);
  EXPECT_EQ(panel.focused_card(), InspectPanel::kEquippedCard);
}

// The two items are the point of the screen, so the set card gives way:
// squeezed into what they leave, and dropped once that is too little.
TEST_F(InspectPanelTest, SqueezesTheSetCardIntoWhatIsLeft) {
  EquipInstance worn = WornHat();
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&hat_);
  panel.SetComparison(&worn);
  int full = NaturalWidth(panel);

  panel.SetMaxColumns(full - 5);
  EXPECT_EQ(NaturalWidth(panel), full - 5);
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Frozen Set"), std::string::npos);
  EXPECT_NE(rendered.find("Frozen Hat"), std::string::npos)
      << "neither item card was cut";
}

TEST_F(InspectPanelTest, DropsTheSetCardWhenThereIsNoRoomToReadIt) {
  EquipInstance worn = WornHat();
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&hat_);
  panel.SetComparison(&worn);
  panel.SetMaxColumns(NaturalWidth(panel) - 40);
  std::string rendered = RenderWide(panel);
  EXPECT_EQ(rendered.find("Frozen Set"), std::string::npos);
  EXPECT_NE(rendered.find("Frozen Hat"), std::string::npos);

  panel.SwapCard(1);
  EXPECT_EQ(panel.focused_card(), InspectPanel::kEquippedCard)
      << "the ring walks what is on screen";
}

// The focus may be on the set card when the terminal narrows. The next render
// moves it instead of leaving the arrows nowhere.
TEST_F(InspectPanelTest, TakingTheSetCardMovesTheFocusOffIt) {
  EquipInstance worn = WornHat();
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&hat_);
  panel.SetComparison(&worn);
  RenderWide(panel);
  ASSERT_TRUE(panel.SwapCard(1));
  ASSERT_EQ(panel.focused_card(), InspectPanel::kSetCard);

  panel.SetMaxColumns(NaturalWidth(panel) - 40);
  RenderWide(panel);
  EXPECT_EQ(panel.focused_card(), InspectPanel::kItemCard);
}

// The row drawn below the one containing `text`, for checking that a rule is in
// the right place.
std::string LineAfter(const std::string& rendered, const std::string& text) {
  size_t at = rendered.find(text);
  if (at == std::string::npos) {
    return "";
  }
  size_t eol = rendered.find('\n', at);
  if (eol == std::string::npos) {
    return "";
  }
  return rendered.substr(eol + 1, rendered.find('\n', eol + 1) - eol - 1);
}

// --- the combat power delta ---

// The number is on the level's row with its label above it, so the widest thing
// on the card isn't a two-word label.
TEST_F(InspectPanelTest, WritesTheCombatPowerDeltaOverTheRequiredLevel) {
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&hat_);
  panel.SetCombatPowerDelta(123456);
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Combat Power \u0394"), std::string::npos)
      << rendered;
  EXPECT_NE(rendered.find("+123,456"), std::string::npos) << rendered;
  EXPECT_NE(LineAfter(rendered, "Combat Power \u0394").find("Req Lev"),
            std::string::npos)
      << "the label sits directly over the level's row";
  size_t row = rendered.find("Req Lev");
  EXPECT_LT(row, rendered.find("+123,456"))
      << "the figure is on the level's own row, right of it";
}

// Green for a gain, red for a loss, and no colour for an item that changes
// nothing: zero isn't a warning.
TEST_F(InspectPanelTest, PaintsTheDeltaBySign) {
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&hat_);

  panel.SetCombatPowerDelta(4200);
  ftxui::Screen gain = Draw(panel);
  EXPECT_EQ(ColorOf(gain, "+4,200"), kGreen);

  panel.SetCombatPowerDelta(-4200);
  ftxui::Screen loss = Draw(panel);
  EXPECT_EQ(ColorOf(loss, "-4,200"), kRed);

  // No sign and no colour, since an item that changes nothing isn't a warning.
  panel.SetCombatPowerDelta(0);
  ftxui::Screen even = Draw(panel);
  EXPECT_NE(ColorOf(even, " 0 "), kGreen);
  EXPECT_NE(ColorOf(even, " 0 "), kRed);
}

// Unset draws neither row, which every screen showing an item the player isn't
// comparing gets. Setting a new item clears it, as it clears the comparison.
TEST_F(InspectPanelTest, NoDeltaRowsWithoutOne) {
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&hat_);
  EXPECT_EQ(RenderWide(panel).find("Combat Power"), std::string::npos);

  panel.SetCombatPowerDelta(7);
  ASSERT_NE(RenderWide(panel).find("Combat Power"), std::string::npos);
  panel.SetItem(&hat_);
  EXPECT_EQ(RenderWide(panel).find("Combat Power"), std::string::npos);
}

// Only one delta on screen. The Equipped card is what the delta is measured
// against, so a delta on it would compare it with itself.
TEST_F(InspectPanelTest, TheEquippedCardCarriesNoDelta) {
  EquipInstance worn = WornHat();
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&hat_);
  panel.SetComparison(&worn);
  panel.SetCombatPowerDelta(500);
  std::string rendered = RenderWide(panel);
  size_t first = rendered.find("Combat Power");
  ASSERT_NE(first, std::string::npos);
  EXPECT_EQ(rendered.find("Combat Power", first + 1), std::string::npos)
      << rendered;
}

// --- Arcane Symbols ---

// How many times `glyph` appears, for counting a growth bar's pips.
int Count(const std::string& rendered, const std::string& glyph) {
  int found = 0;
  for (size_t at = rendered.find(glyph); at != std::string::npos;
       at = rendered.find(glyph, at + glyph.size())) {
    ++found;
  }
  return found;
}

// A symbol grants nothing an equip's rows could show, so it gets its own card:
// its level progress, and what that level is worth.
TEST_F(InspectPanelTest, ASymbolCardIsItsLevelExpStatAndForce) {
  Character proto;
  proto.set_level(200);
  proto.set_job(JOB_HERO);
  proto.set_job_stage(4);
  CharacterInstance hero(rng_, std::move(proto));

  Equip state;
  state.set_symbol_level(8);
  state.set_symbol_exp(12);
  EquipInstance symbol(VanishingJourneySymbol(), state);

  InspectPanel panel;
  panel.UseCharacter(hero);
  panel.SetItem(&symbol);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Growth Level  8"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("EXP  12 / 75"), std::string::npos) << rendered;
  // Growth progress isn't a stat, so a rule separates the two.
  EXPECT_NE(LineAfter(rendered, "EXP  12 / 75").find("──"), std::string::npos)
      << rendered;
  EXPECT_NE(rendered.find("STR  +1000"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Arcane Force  +100"), std::string::npos) << rendered;
  // The same heading an equip has, since a symbol has the same two facts to
  // show.
  EXPECT_NE(rendered.find("Req Lev: 200"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Warrior"), std::string::npos) << rendered;
  // A symbol takes no scrolls and no star force, so neither row appears.
  EXPECT_EQ(rendered.find("Successful Scroll"), std::string::npos);
  EXPECT_EQ(rendered.find("★"), std::string::npos);
  EXPECT_EQ(rendered.find("☆"), std::string::npos);
  // Instead, a pip per level, filled up to the symbol's level.
  EXPECT_EQ(Count(rendered, "◆"), 8) << rendered;
  EXPECT_EQ(Count(rendered, "◇"), kMaxSymbolLevel - 8) << rendered;
}

// The stat a symbol grants is the wearer's primary stat, so a magician sees INT
// on the same item a warrior sees STR on.
TEST_F(InspectPanelTest, TheSymbolStatFollowsTheWearer) {
  Character proto;
  proto.set_level(200);
  proto.set_job(JOB_BISHOP);
  proto.set_job_stage(4);
  CharacterInstance bishop(rng_, std::move(proto));

  EquipInstance symbol(VanishingJourneySymbol());
  InspectPanel panel;
  panel.UseCharacter(bishop);
  panel.SetItem(&symbol);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("INT  +300"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Arcane Force  +30"), std::string::npos) << rendered;
}

// A maxed symbol has no next level, so the row says so instead of showing a bar
// that can never fill.
TEST_F(InspectPanelTest, AMaxedSymbolReadsMax) {
  Equip state;
  state.set_symbol_level(kMaxSymbolLevel);
  EquipInstance symbol(VanishingJourneySymbol(), state);
  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetItem(&symbol);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("EXP  MAX"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Arcane Force  +220"), std::string::npos) << rendered;
  EXPECT_EQ(Count(rendered, "◆"), kMaxSymbolLevel) << rendered;
  EXPECT_EQ(Count(rendered, "◇"), 0) << rendered;
}

// --- the ring and pendant tab bar ---

// A ring to wear or compare, with a distinct name so a rendered card shows
// which is which.
EquipInstance Ring(const std::string& name) {
  return EquipInstance(FrozenPiece(name, EQUIP_SLOT_RING));
}

// The bar as drawn: every chip is padded with a space on each side.
constexpr char kFourChips[] = " 1  2  3  4 ";

TEST_F(InspectPanelTest, DrawsAChipPerSlotAndTheActiveSlotsItem) {
  EquipInstance first = Ring("First Ring");
  EquipInstance third = Ring("Third Ring");
  EquipInstance inspected = Ring("New Ring");
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&inspected);
  panel.SetComparison({{&first, nullptr, &third, nullptr}, /*active=*/2});

  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find(kFourChips), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Third Ring"), std::string::npos) << rendered;
  EXPECT_EQ(rendered.find("First Ring"), std::string::npos)
      << "one slot at a time, whichever the bar is on";
}

// An empty slot is still a slot the player can compare the ring with: the bar
// names it and the card says it is empty.
TEST_F(InspectPanelTest, AnEmptySlotIsABarOverAnEmptyCard) {
  EquipInstance inspected = Ring("New Ring");
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&inspected);
  panel.SetComparison({{nullptr, nullptr, nullptr, nullptr}, /*active=*/1});

  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Equipped"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find(kFourChips), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("(empty)"), std::string::npos) << rendered;
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty())
      << "the bar is the widest row of a card with nothing else on it";
}

// Every item except a ring or a pendant has one slot and nothing to choose
// between: no bar, and an empty slot draws no card at all.
TEST_F(InspectPanelTest, OneSlotDrawsNoBar) {
  EquipInstance worn = WornHat();
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&hat_);
  panel.SetComparison(&worn);
  EXPECT_EQ(RenderWide(panel).find(" 1  2 "), std::string::npos);

  panel.SetComparison(static_cast<const EquipTabItem*>(nullptr));
  EXPECT_EQ(RenderWide(panel).find("Equipped"), std::string::npos);
}

// The ring moves through what is on screen, and a card of four empty slots is
// on screen.
TEST_F(InspectPanelTest, TheBarPutsTheEquippedCardOnTheRing) {
  EquipInstance inspected = Ring("New Ring");
  InspectPanel panel = TallPanel(c_, 34);
  panel.SetItem(&inspected);
  panel.SetComparison({{nullptr, nullptr, nullptr, nullptr}, 0});
  RenderWide(panel);

  ASSERT_TRUE(panel.SwapCard(-1));
  EXPECT_EQ(panel.focused_card(), InspectPanel::kEquippedCard);
}

// The item card, the card beside it and the stackable card are three different
// windows, each fitted to its own rows.
TEST_F(InspectPanelTest, NoCardWeldsARowToItsRightBorder) {
  InspectPanel panel;
  panel.UseCharacter(c_);
  EquipInstance item(sword_);
  panel.SetItem(&item);
  panel.SetComparison(&item);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.RenderItemOnly()).empty());
}
}  // namespace
}  // namespace ms
