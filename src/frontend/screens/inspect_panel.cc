#include "src/frontend/screens/inspect_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/arcane_force.h"
#include "src/combat/damage.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/scroll_card.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// The Equipped card's title. Two paths draw that card -- one slot or a bar of
// them -- and they must name it the same.
constexpr char kEquippedTitle[] = " Equipped ";

// The width of the stackable body. Fixed rather than fitted, so every
// description reads at the same width and the window does not resize as the
// cursor moves from one item to the next. The equip body sets its own width
// from its columns.
constexpr int kStackableWidth = 44;

// The set card's rows. One width whatever the set holds, for the same reason
// the stackable body has one: the card sits beside the item, and a card that
// resized would walk the item panel across the screen. The borders and the
// scroll bar's column are three more.
constexpr int kSetContentWidth = 45;
constexpr int kSetSlotWidth = 11;
constexpr int kSetTierWidth = 15;
// The borders and the bar's column, and so what the card costs beyond its
// rows. Its full width, and the narrowest squeeze it is worth drawing at: a
// set card holding less than this cuts the tier values off every line, which
// is the half of each row a reader came for.
constexpr int kSetCardChrome = 3;
constexpr int kSetCardWidth = kSetContentWidth + kSetCardChrome;
constexpr int kSetCardMinWidth = 36 + kSetCardChrome;

// Stars to a rank when the bar is folded. Fifteen because that is where GMS
// splits the two star force regimes, and because a 30-star bar reads as two
// even ranks.
constexpr int kStarsPerRow = 15;

// The job categories, in the order every screen lists them.
struct JobCategoryEntry {
  const char* name;
  EquipJobCategory category;
};

const JobCategoryEntry kJobCategories[] = {
    {"Beginner", EQUIP_JOB_CATEGORY_BEGINNER},
    {"Warrior", EQUIP_JOB_CATEGORY_WARRIOR},
    {"Bowman", EQUIP_JOB_CATEGORY_BOWMAN},
    {"Magician", EQUIP_JOB_CATEGORY_MAGICIAN},
    {"Thief", EQUIP_JOB_CATEGORY_THIEF},
    {"Pirate", EQUIP_JOB_CATEGORY_PIRATE},
};

constexpr int kJobCategoryCount =
    static_cast<int>(sizeof(kJobCategories) / sizeof(kJobCategories[0]));

// One row of a symbol's card, for the two figures that are not a stat. Spaced
// as the stat rows under them, so the whole block reads as one column.
ftxui::Element SymbolRow(const std::string& label, const std::string& value) {
  return ftxui::text(" " + label + "  " + value + " ");
}

void Append(std::vector<CardRow>& rows, const std::vector<CardRow>& more) {
  rows.insert(rows.end(), more.begin(), more.end());
}

void Append(std::vector<ftxui::Element>& cards,
            const std::vector<ftxui::Element>& more) {
  cards.insert(cards.end(), more.begin(), more.end());
}

// The columns a built card asks for.
int Columns(const ftxui::Element& card) {
  card->ComputeRequirement();
  return card->requirement().min_x;
}

// A fraction as a percentage, with a whole number left whole: a set's figures
// are written round, and "+20.00%" says nothing "+20%" does not.
std::string SetPercent(double fraction) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f", std::round(fraction * 1000.0) / 10.0);
  std::string s = buf;
  if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) {
    s.resize(s.size() - 2);
  }
  return s + "%";
}

// Two levers a set states as one row when both halves agree. They virtually
// always come bundled -- a set that pays attack pays magic attack with it --
// and two rows saying the same number is two rows the player has to compare.
template <typename T>
struct LeverPair {
  const char* together;
  const char* first;
  const char* second;
  T (SkillEffect::*a)() const;
  T (SkillEffect::*b)() const;
};

const LeverPair<double> kFlatPairs[] = {
    {"Max HP & MP", "Max HP", "Max MP", &SkillEffect::max_hp,
     &SkillEffect::max_mp},
    {"Attack Power & Magic ATT", "Attack Power", "Magic ATT",
     &SkillEffect::attack, &SkillEffect::magic_attack},
};

const LeverPair<double> kPercentPairs[] = {
    {"Max HP & MP", "Max HP", "Max MP", &SkillEffect::max_hp_pct,
     &SkillEffect::max_mp_pct},
};

// The levers a set states on their own, in display order.
struct FlatLever {
  const char* label;
  double (SkillEffect::*fn)() const;
};

const FlatLever kFlatLevers[] = {
    {"DEF", &SkillEffect::def},
};

struct PercentLever {
  const char* label;
  double (SkillEffect::*fn)() const;
};

const PercentLever kPercentLevers[] = {
    {"ATT", &SkillEffect::attack_pct},
    {"Damage", &SkillEffect::damage_pct},
    {"Boss Damage", &SkillEffect::boss_pct},
    {"Ignore DEF", &SkillEffect::ied_pct},
    {"Critical Rate", &SkillEffect::crit_rate},
    {"Critical Damage", &SkillEffect::crit_dmg},
    {"Meso Drop Rate", &SkillEffect::meso_pct},
    {"Item Drop Rate", &SkillEffect::item_drop_pct},
    {"Additional EXP", &SkillEffect::exp_pct},
};

struct StatValue {
  const char* label;
  int value;
};

// The four primary stats, which a set grants in equal shares or not at all.
void AppendStatLines(const SkillEffect& e, std::vector<std::string>& lines) {
  if (e.str() != 0 && e.str() == e.dex() && e.dex() == e.int_() &&
      e.int_() == e.luk()) {
    lines.push_back("All Stats +" + std::to_string(WholeValue(e.str())));
    return;
  }
  const StatValue kStats[] = {{"STR", WholeValue(e.str())},
                              {"DEX", WholeValue(e.dex())},
                              {"INT", WholeValue(e.int_())},
                              {"LUK", WholeValue(e.luk())}};
  for (const StatValue& stat : kStats) {
    if (stat.value != 0) {
      lines.push_back(std::string(stat.label) + " +" +
                      std::to_string(stat.value));
    }
  }
}

// A flat lever's own figure, so both kinds of pair share one shape. A set
// bonus states whole numbers, but every SkillEffect number is a double -- see
// WholeValue.
std::string SetWhole(double value) {
  return std::to_string(WholeValue(value));
}

// Adds a pair as one row when both halves agree, and as a row each when they
// do not: a set is free to pay one side more than the other.
template <typename T, typename Format>
void AppendPairLines(const LeverPair<T>& pair, const SkillEffect& e,
                     Format format, std::vector<std::string>& lines) {
  T a = (e.*pair.a)();
  T b = (e.*pair.b)();
  if (a != T() && a == b) {
    lines.push_back(std::string(pair.together) + " +" + format(a));
    return;
  }
  if (a != T()) {
    lines.push_back(std::string(pair.first) + " +" + format(a));
  }
  if (b != T()) {
    lines.push_back(std::string(pair.second) + " +" + format(b));
  }
}

// What one tier of a set pays, a line per lever. A lever with no row here is
// a bonus the player is paid and never told about, which the set data test
// watches for.
std::vector<std::string> EffectLines(const SkillEffect& e) {
  std::vector<std::string> lines;
  AppendStatLines(e, lines);
  for (const FlatLever& lever : kFlatLevers) {
    int value = WholeValue((e.*lever.fn)());
    if (value != 0) {
      lines.push_back(std::string(lever.label) + " +" + std::to_string(value));
    }
  }
  for (const LeverPair<double>& pair : kPercentPairs) {
    AppendPairLines(pair, e, SetPercent, lines);
  }
  for (const LeverPair<double>& pair : kFlatPairs) {
    AppendPairLines(pair, e, SetWhole, lines);
  }
  for (const PercentLever& lever : kPercentLevers) {
    double value = (e.*lever.fn)();
    if (value != 0.0) {
      lines.push_back(std::string(lever.label) + " +" + SetPercent(value));
    }
  }
  return lines;
}

}  // namespace

void InspectPanel::SetItem(const EquipTabItem* item) {
  item_ = item;
  stackable_ = nullptr;
  compare_ = {};
  delta_.reset();
}

void InspectPanel::SetItem(const ItemPrototype* item) {
  stackable_ = item;
  item_ = nullptr;
  compare_ = {};
  delta_.reset();
}

void InspectPanel::SetComparison(const EquipTabItem* equipped) {
  compare_ = {{equipped}, 0};
}

void InspectPanel::SetComparison(ComparisonSlots slots) {
  compare_ = std::move(slots);
}

const EquipTabItem* InspectPanel::Compared() const {
  if (compare_.active < 0 ||
      compare_.active >= static_cast<int>(compare_.worn.size())) {
    return nullptr;
  }
  return compare_.worn[compare_.active];
}

bool InspectPanel::HasComparisonCard() const {
  return compare_.worn.size() > 1 || Compared() != nullptr;
}

void InspectPanel::SetCombatPowerDelta(std::optional<int> delta) {
  delta_ = delta;
}

void InspectPanel::UseCharacter(const CharacterInstance& character) {
  character_ = &character;
}

void InspectPanel::SetMaxRows(int rows) {
  item_card_.SetMaxRows(rows);
  set_card_.SetMaxRows(rows);
  compare_card_.SetMaxRows(rows);
}

void InspectPanel::SetMaxColumns(int columns) {
  max_columns_ = columns;
}

void InspectPanel::ScrollBy(int delta) {
  FocusedCard().ScrollBy(delta);
}

void InspectPanel::ScrollXBy(int delta) {
  FocusedCard().ScrollXBy(delta);
}

const ScrollCard& InspectPanel::FocusedCard() const {
  switch (focus_) {
    case kSetCard:
      return set_card_;
    case kEquippedCard:
      return compare_card_;
    case kItemCard:
      break;
  }
  return item_card_;
}

ScrollCard& InspectPanel::FocusedCard() {
  return const_cast<ScrollCard&>(std::as_const(*this).FocusedCard());
}

std::vector<InspectPanel::Card> InspectPanel::DrawnCards() const {
  std::vector<Card> cards;
  if (HasComparisonCard()) {
    cards.push_back(kEquippedCard);
  }
  cards.push_back(kItemCard);
  if (set_drawn_) {
    cards.push_back(kSetCard);
  }
  return cards;
}

bool InspectPanel::SwapCard(int step) {
  std::vector<Card> cards = DrawnCards();
  int count = static_cast<int>(cards.size());
  if (count < 2) {
    return false;
  }
  int at = 0;
  for (int i = 0; i < count; ++i) {
    if (cards[i] == focus_) {
      at = i;
    }
  }
  // Stepped by count + step so a backwards walk never hands the modulo a
  // negative, and both directions are one path.
  focus_ = cards[(at + count + step % count) % count];
  return true;
}

void InspectPanel::Reset() {
  item_card_.Reset();
  set_card_.Reset();
  compare_card_.Reset();
  focus_ = kItemCard;
}

bool InspectPanel::HasSetCard() const {
  return SetOfItem() != nullptr;
}

ftxui::Element InspectPanel::RenderItemOnly(bool focused,
                                            const std::string& title) const {
  return RenderCard(item_card_, item_, title, focused);
}

ftxui::Element InspectPanel::RenderComparison(bool focused) const {
  const EquipTabItem* worn = Compared();
  // One slot is every item but a ring or a pendant: whatever is worn there,
  // framed like any other card and with nothing to choose between.
  if (compare_.worn.size() < 2) {
    return RenderCard(compare_card_, worn, kEquippedTitle, focused);
  }
  std::vector<TabSpec> specs;
  for (int i = 1; i <= static_cast<int>(compare_.worn.size()); ++i) {
    specs.push_back({std::to_string(i)});
  }
  // No width to fit into: four chips never come near what a card is drawn at,
  // so the bar cannot scroll. The blank column inside the right border is
  // added by hand -- a chip carries its own background and would otherwise
  // weld to it on a card the bar is the widest row of.
  CardRows rows;
  rows.head.push_back(
      TextRow(ftxui::hbox({TabBar(specs, compare_.active, focused, /*width=*/0),
                           ftxui::text(" ")})));
  if (worn == nullptr) {
    rows.body.push_back(TextRow(CenteredRow("(empty)")));
    return compare_card_.Render(kEquippedTitle, std::move(rows),
                                /*content_width=*/0, focused);
  }
  // The bar sits over the card's own head, with no rule under it: the chips
  // and the item they name read as one thing.
  CardRows body = EquipRows(*worn);
  Append(rows.head, body.head);
  rows.body = std::move(body.body);
  rows.foot = std::move(body.foot);
  return compare_card_.Render(kEquippedTitle, std::move(rows),
                              /*content_width=*/0, focused);
}

ftxui::Element InspectPanel::RenderCard(const ScrollCard& card,
                                        const EquipTabItem* item,
                                        const std::string& title,
                                        bool focused) const {
  // The stackable body is a paragraph, which wraps to as many lines as it
  // needs and so cannot be sliced into a scrolling window. It is two lines at
  // its longest, and never outgrows a terminal. Only the inspected item is
  // ever one: nothing stackable is worn, so nothing is compared against one.
  if (item == item_ && stackable_ != nullptr) {
    return ThemedWindow(title, RenderStackable(), focused) |
           ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kStackableWidth);
  }
  if (item == nullptr) {
    return ThemedWindow(title, EmptyState("no item"), focused);
  }
  CardRows rows =
      IsArcaneSymbol(item->prototype()) ? SymbolRows(*item) : EquipRows(*item);
  return card.Render(title, std::move(rows), /*content_width=*/0, focused);
}

CardRows InspectPanel::SymbolRows(const EquipTabItem& item) const {
  int level = SymbolLevel(item.equip_state());
  std::vector<CardRow> head = HeadRows(item);
  std::vector<CardRow> stats = SymbolStatRows(item, level);
  std::vector<CardRow> jobs =
      JobRows(item, std::max(NaturalWidth(head), NaturalWidth(stats)));

  CardRows rows;
  // The growth bar takes the star bar's place, and the name, the level it asks
  // for and the jobs it is for are the same head an equip carries. Nothing
  // here scrolls: a symbol has four rows to say and a terminal always has room
  // for four.
  rows.head = {TextRow(CenteredRow(SymbolBar(level)))};
  Append(rows.head, head);
  Append(rows.head, jobs);
  rows.head.push_back(RuleRow(ThemedSeparator()));
  rows.body = std::move(stats);
  return rows;
}

// Where the symbol stands and what that is worth, in the equip card's own stat
// spacing. A rule splits the two: how far it has grown is not a stat it pays.
// The stat it grants is the wearer's own, so a card with nobody behind it
// shows the force alone rather than guessing at a job.
std::vector<CardRow> InspectPanel::SymbolStatRows(const EquipTabItem& item,
                                                  int level) const {
  int needed = SymbolExpToNextLevel(level);
  std::vector<CardRow> rows = {
      TextRow(SymbolRow("Growth Level", std::to_string(level))),
      TextRow(SymbolRow(
          "EXP", needed == 0 ? "MAX"
                             : std::to_string(item.equip_state().symbol_exp()) +
                                   " / " + std::to_string(needed))),
      RuleRow(ThemedSeparator()),
  };
  if (character_ != nullptr) {
    StatField primary = PrimaryStatField(character_->proto().job());
    const DisplayStat* stat = DisplayStatFor(primary);
    if (stat != nullptr) {
      rows.push_back(TextRow(StatLine(
          stat->label, stat->GetFrom(SymbolStatsFor(primary, level)), 0)));
    }
  }
  rows.push_back(
      TextRow(StatLine("Arcane Force", SymbolArcaneForce(level), 0)));
  return rows;
}

ftxui::Element InspectPanel::Render() const {
  const EquipSet* set = SetOfItem();
  set_drawn_ = false;
  // A card lights its title only when there is a second one to tell it from:
  // on a screen with one card the arrows have nowhere else to go.
  if (set == nullptr && !HasComparisonCard()) {
    return RenderItemOnly();
  }
  std::vector<ftxui::Element> cards;
  if (HasComparisonCard()) {
    cards.push_back(RenderComparison(focus_ == kEquippedCard));
  }
  cards.push_back(RenderItemOnly(focus_ == kItemCard));
  if (set != nullptr) {
    int used = 0;
    for (const ftxui::Element& card : cards) {
      used += Columns(card);
    }
    // No limit means every card at its own width, which is what a test and a
    // terminal with room to spare both want.
    Append(cards, SetCardIn(*set, max_columns_ > 0 ? max_columns_ - used
                                                   : kSetCardWidth));
  }
  // The focus can be left on a card the width has since taken. Nothing is
  // lost by moving it: the item card is where the screen opens anyway.
  if (focus_ == kSetCard && !set_drawn_) {
    focus_ = kItemCard;
  }
  return ftxui::hbox(std::move(cards));
}

// Squeezed to what the other cards leave, and left out once that is too little
// for its rows to read: a set card holding two words of every line says less
// than the columns it costs the cards beside it.
std::vector<ftxui::Element> InspectPanel::SetCardIn(const EquipSet& set,
                                                    int room) const {
  if (room < kSetCardMinWidth) {
    return {};
  }
  set_drawn_ = true;
  int view = room >= kSetCardWidth ? 0 : room - kSetCardChrome;
  return {set_card_.Render(" Set Effect ", SetRows(set), kSetContentWidth,
                           focus_ == kSetCard, view)};
}

const EquipSet* InspectPanel::SetOfItem() const {
  if (item_ == nullptr || character_ == nullptr) {
    return nullptr;
  }
  // By display name, as the character counts what is worn. A trace of a set
  // piece is a piece of that set: it is the same item, waiting to be recovered.
  // An item the set names by family answers to its family instead.
  const std::string& name = item_->prototype().name();
  const std::string& family = item_->prototype().set_family();
  for (const std::pair<const std::string, EquipSet>& entry :
       character_->equip_sets()) {
    for (const EquipSetMember& member : entry.second.members()) {
      for (const std::string& fills : member.items().name()) {
        if (fills == name) {
          return &entry.second;
        }
      }
      if (!family.empty() && member.family() == family) {
        return &entry.second;
      }
    }
  }
  return nullptr;
}

// One row of a set's piece list: what fills the slot, and whether it is on.
struct SetFill {
  std::string text;
  bool worn;
};

std::vector<CardRow> InspectPanel::MemberRows(
    const EquipSetMember& member) const {
  std::vector<SetFill> fills;
  for (const std::string& name : member.items().name()) {
    fills.push_back({name, character_->IsWearing(name)});
  }
  // A family names what fills it once something does, and asks for one while
  // it is empty -- a weapon belongs to a class, so the set cannot name it
  // outright. It hangs under whatever the slot names by hand, since the
  // written pieces are the ones a player can go and look up.
  if (member.has_family()) {
    std::string on = character_->WornOfFamily(member.family());
    fills.push_back(
        {on.empty() ? "Choose 1 " + member.family() : on, !on.empty()});
  }
  bool filled = false;
  for (const SetFill& fill : fills) {
    filled = filled || fill.worn;
  }

  // A slot filled by any of several alternates lists them all, the later ones
  // hanging under the slot they share as a tier's later lines do.
  std::vector<CardRow> rows;
  std::string slot = FormatSlot(member.slot());
  for (const SetFill& fill : fills) {
    ftxui::Element label = ftxui::text(" " + PadRight(slot, kSetSlotWidth));
    ftxui::Element name = ftxui::text(fill.text);
    // Dimmed unless it is on the character right now -- the same question the
    // tiers below are asking, one piece at a time. The slot is asking a
    // narrower one: whether it is filled at all. So it stays lit whichever
    // alternate fills it, and however many of them do.
    if (!filled) {
      label = label | ftxui::dim;
    }
    if (!fill.worn) {
      name = name | ftxui::dim;
    }
    rows.push_back(TextRow(ftxui::hbox({std::move(label), std::move(name)})));
    slot.clear();
  }
  return rows;
}

CardRows InspectPanel::SetRows(const EquipSet& set) const {
  int worn = character_->PiecesWornOf(set);
  // The set's name and what it is made of are held at the head: the tiers
  // below read as what those pieces would pay, so they are the part that
  // scrolls and the pieces stay beside them.
  CardRows card;
  std::vector<CardRow>& rows = card.head;
  rows.push_back(TextRow(CenteredRow(FormatEquipSet(set.name()))));
  rows.push_back(RuleRow(ThemedSeparator()));
  for (const EquipSetMember& member : set.members()) {
    Append(rows, MemberRows(member));
  }
  rows.push_back(RuleRow(ThemedSeparator()));
  for (const EquipSetTier& tier : set.tiers()) {
    std::string label = std::to_string(tier.pieces()) + " Set Effect";
    for (const std::string& line : EffectLines(tier.effect())) {
      ftxui::Element row =
          ftxui::text(" " + PadRight(label, kSetTierWidth) + line);
      // Dimmed until the pieces are on: the card is what the set would pay,
      // and the stats page is what the character has. This is dim's softer
      // reading -- not in play rather than refused (colors.h). Nothing on this
      // card is being turned down, so the two do not collide here.
      if (worn < tier.pieces()) {
        row = row | ftxui::dim;
      }
      card.body.push_back(TextRow(std::move(row)));
      // Only the first line of a tier is labelled; the rest hang under it.
      label.clear();
    }
  }
  return card;
}

// A stack has no stats, no stars and no slots. Its name and what it is for is
// the whole of what there is to say about it.
ftxui::Element InspectPanel::RenderStackable() const {
  ftxui::Element description;
  if (stackable_->description().empty()) {
    description = CenteredRow(EmptyState("no description", /*gutter=*/0));
  } else {
    // paragraph wraps on spaces, so a description longer than the window
    // spills onto another line rather than off the edge. Spaced off both
    // borders by hand: every other row carries its own gutter in its string,
    // and a paragraph has no string to put one in.
    description = ftxui::hbox({
        ftxui::text(" "),
        ftxui::paragraph(stackable_->description()),
        ftxui::text(" "),
    });
  }
  return ftxui::vbox({
      CenteredRow(stackable_->name()),
      ThemedSeparator(),
      std::move(description),
  });
}

// The rows above the job categories: the item's name, the level it asks for,
// and what wearing it would do to the player's combat power.
std::vector<CardRow> InspectPanel::HeadRows(const EquipTabItem& item) const {
  int level = item.prototype().required_level();
  std::vector<CardRow> delta = DeltaRows(item);
  std::vector<CardRow> rows = {
      TextRow(CenteredRow(item.name())),
      RuleRow(ThemedSeparator()),
  };
  // The label sits over the figure rather than beside it: "Combat Power Δ" is
  // wider than anything else on the card, and on the level's own row it would
  // set the width of the whole panel.
  if (delta.size() == 2) {
    rows.push_back(std::move(delta[0]));
  }
  // Trailing space on each text row keeps the right border one column clear.
  ftxui::Element req =
      ftxui::text(" Req Lev: " + std::to_string(level > 0 ? level : 1) + " ");
  if (delta.empty()) {
    rows.push_back(TextRow(std::move(req)));
    return rows;
  }
  rows.push_back(TextRow(ftxui::hbox({
      std::move(req),
      ftxui::filler(),
      std::move(delta[1].element),
  })));
  return rows;
}

// What the figure is painted in: green for a gain, red for a loss, and the
// plain colour for an item that changes nothing.
std::vector<CardRow> InspectPanel::DeltaRows(const EquipTabItem& item) const {
  if (&item != item_ || !delta_.has_value()) {
    return {};
  }
  const int delta = *delta_;
  std::string figure =
      delta == 0 ? "0"
                 : (delta > 0 ? "+" : "-") + FormatWithCommas(std::abs(delta));
  ftxui::Element value = ftxui::text(figure + " ");
  if (delta > 0) {
    value = std::move(value) | ftxui::color(kGreen);
  } else if (delta < 0) {
    value = std::move(value) | ftxui::color(kRed);
  }
  return {
      TextRow(
          ftxui::hbox({ftxui::filler(), ftxui::text("Combat Power \u0394 ")})),
      TextRow(std::move(value)),
  };
}

// What the item grants: its kind, its speed and its stats. The scrolling part
// of the card, since it is the part that outgrows a short terminal.
std::vector<CardRow> InspectPanel::StatRows(const EquipTabItem& item) const {
  const EquipPrototype& proto = item.prototype();
  const Equip& item_state = item.equip_state();

  std::vector<CardRow> rows;
  if (proto.equip_type() != EQUIP_TYPE_UNSPECIFIED) {
    rows.push_back(TextRow(
        ftxui::text(" Type: " + FormatEquipType(proto.equip_type()) + " ")));
  }
  if (proto.attack_speed() != ATTACK_SPEED_UNSPECIFIED) {
    rows.push_back(TextRow(ftxui::text(
        " Attack Speed: " + FormatAttackSpeed(proto.attack_speed()) + " ")));
  }

  const EquipStats& base = proto.base_stats();
  const EquipStats& scroll = item_state.scroll_stats();
  EquipStats sf = item.StarForceStatGains();
  bool any_stat = false;
  for (const DisplayStat& stat : kDisplayStats) {
    ftxui::Element row = StatLine(stat.label, stat.GetFrom(base),
                                  stat.GetFrom(scroll), stat.GetFrom(sf));
    if (row == nullptr) {
      continue;
    }
    rows.push_back(TextRow(std::move(row)));
    any_stat = true;
  }
  // Under the flat stats, since a share of a pool is read against the pile the
  // rows above build. Nothing but the prototype grants one.
  for (const DisplayStat& stat : kDisplayPercentStats) {
    int value = stat.GetFrom(base);
    if (value == 0) {
      continue;
    }
    rows.push_back(TextRow(ftxui::text(" " + std::string(stat.label) + "  +" +
                                       std::to_string(value) + "% ")));
    any_stat = true;
  }
  if (!any_stat) {
    rows.push_back(TextRow(EmptyState("no stats")));
  }
  return rows;
}

// What the item has spent, held at the foot of the card: an item's upgrade
// history belongs with the item rather than at the end of a list the reader
// has to scroll to. Empty for an item that takes no scrolls at all.
std::vector<CardRow> InspectPanel::SlotRows(const EquipTabItem& item) const {
  const EquipPrototype& proto = item.prototype();
  const Equip& item_state = item.equip_state();
  int slots = TotalUpgradeSlots(proto, item_state);
  if (slots <= 0) {
    return {};
  }
  int pass = item_state.scroll_successes();
  int left = item_state.remaining_upgrade_slots();
  int restore = slots - pass - left;
  std::string scroll_label =
      pass == 1 ? " Successful Scroll " : " Successful Scrolls ";
  std::string restore_label = restore == 1 ? " Restore) " : " Restores) ";
  return {
      RuleRow(ThemedSeparator()),
      TextRow(ftxui::text(" " + std::to_string(pass) + scroll_label)),
      TextRow(ftxui::text(" (" + std::to_string(left) + " Left, " +
                          std::to_string(restore) + restore_label)),
  };
}

// The rolled lines. The rank is named once at the head, and each line's own
// dot says whether it came out prime: a line a rung down wears the colour of
// the rank below the header's.
std::vector<CardRow> InspectPanel::PotentialRows(
    const EquipTabItem& item) const {
  const Potential& potential = item.potential();
  if (potential.rank() == POTENTIAL_RANK_UNSPECIFIED) {
    return {};
  }
  const std::string rank = PotentialRankName(potential.rank());
  const ftxui::Color rank_color = RarityColor(potential.rank());
  std::vector<CardRow> rows = {
      RuleRow(ThemedSeparator()),
      TextRow(ftxui::text(" " + rank + " Potential ") |
              ftxui::color(rank_color)),
  };
  const int level = item.prototype().required_level();
  for (const PotentialLine& line : potential.lines()) {
    rows.push_back(TextRow(ftxui::hbox({
        ftxui::text(" ◼") | ftxui::color(RarityColor(line.rank())),
        ftxui::text("  " + PotentialLineName(line.type()) + "  " +
                    PotentialLineValueText(line, level) + " "),
    })));
  }
  return rows;
}

CardRows InspectPanel::EquipRows(const EquipTabItem& item) const {
  std::vector<CardRow> head = HeadRows(item);
  std::vector<CardRow> stats = StatRows(item);
  std::vector<CardRow> slots = SlotRows(item);
  Append(slots, PotentialRows(item));
  // What the item has to say sets the width; the two rows that can be folded
  // are measured against it rather than the other way round.
  int fixed =
      std::max({NaturalWidth(head), NaturalWidth(stats), NaturalWidth(slots)});

  std::vector<CardRow> jobs = JobRows(item, fixed);
  CardRows rows;
  // What names the item stays on screen, and so does what it has spent; the
  // stats between them are what moves.
  rows.head = StarRows(item, std::max(fixed, NaturalWidth(jobs)));
  Append(rows.head, head);
  Append(rows.head, jobs);
  rows.head.push_back(RuleRow(ThemedSeparator()));
  rows.body = std::move(stats);
  rows.foot = std::move(slots);
  return rows;
}

// The six job categories, on one row or folded onto two. Folded whenever the
// one-row form would be what makes the panel wide: the categories are the same
// six on every item, so a card describing a narrow item should not be 55
// columns across to list them.
std::vector<CardRow> InspectPanel::JobRows(const EquipTabItem& item,
                                           int fixed) const {
  const EquipPrototype& proto = item.prototype();
  ftxui::Element one_row = JobRow(proto, 0, kJobCategoryCount);
  if (NaturalWidth({one_row}) <= fixed) {
    return {TextRow(one_row)};
  }
  int half = kJobCategoryCount / 2;
  return {TextRow(CenteredRow(JobRow(proto, 0, half))),
          TextRow(CenteredRow(JobRow(proto, half, kJobCategoryCount - half)))};
}

// The star bar, on one row or folded onto two. An item that refuses star force
// gets no bar at all: a row of empty stars reads as a bar waiting to be
// filled, which is the opposite of the truth.
std::vector<CardRow> InspectPanel::StarRows(const EquipTabItem& item,
                                            int fixed) const {
  const EquipPrototype& proto = item.prototype();
  if (!Supports(proto, UPGRADE_STAR_FORCE)) {
    return {};
  }
  int stars = item.stars();
  int max_stars = item.max_stars();
  ftxui::Element one_row = CenteredRow(StarBar(stars, 0, max_stars));
  if (max_stars <= kStarsPerRow || NaturalWidth({one_row}) <= fixed) {
    return {TextRow(one_row)};
  }
  // A row of fifteen with the excess centred under it, which is how a 30-star
  // bar reads as two ranks rather than one long line.
  return {TextRow(CenteredRow(StarBar(stars, 0, kStarsPerRow))),
          TextRow(CenteredRow(
              StarBar(stars, kStarsPerRow, max_stars - kStarsPerRow)))};
}

ftxui::Element InspectPanel::StarBar(int stars, int from, int count) {
  const ftxui::Color kFilled = kYellow;
  const ftxui::Color kEmpty = kGray;
  std::vector<ftxui::Element> parts;
  for (int i = from; i < from + count; ++i) {
    // Grouped in fives from the start of the row, so a folded bar's second
    // rank groups like its first rather than carrying the first's remainder.
    if (i > from && (i - from) % 5 == 0) {
      parts.push_back(ftxui::text(" "));
    }
    bool filled = i < stars;
    parts.push_back(ftxui::text(filled ? "★" : "☆") |
                    ftxui::color(filled ? kFilled : kEmpty));
  }
  return ftxui::hbox(std::move(parts));
}

// The growth track a symbol has in place of stars: one pip a level, grouped in
// fives like the star bar. Purple rather than gold, since a symbol takes no
// star force and the two bars must not be read for one another.
ftxui::Element InspectPanel::SymbolBar(int level) {
  std::vector<ftxui::Element> parts;
  for (int i = 0; i < kMaxSymbolLevel; ++i) {
    if (i > 0 && i % 5 == 0) {
      parts.push_back(ftxui::text(" "));
    }
    bool filled = i < level;
    parts.push_back(ftxui::text(filled ? "◆" : "◇") |
                    ftxui::color(filled ? kPurple : kGray));
  }
  return ftxui::hbox(std::move(parts));
}

ftxui::Element InspectPanel::StatLine(const std::string& label, int base,
                                      int scroll, int sf) {
  if (base == 0 && scroll == 0 && sf == 0) {
    return nullptr;
  }
  int total = base + scroll + sf;
  // Base-only: no breakdown needed, plain text.
  if (scroll == 0 && sf == 0) {
    return ftxui::text(" " + label + "  +" + std::to_string(total) + " ");
  }
  // Breakdown: base in default color, scroll in periwinkle, SF in gold.
  const ftxui::Color kScrollColor = kPurple;
  const ftxui::Color kSfColor = kGold;
  std::vector<ftxui::Element> parts;
  parts.push_back(
      ftxui::text(" " + label + "  +" + std::to_string(total) + " ("));
  parts.push_back(ftxui::text(std::to_string(base)));
  if (scroll > 0) {
    parts.push_back(ftxui::text(" +" + std::to_string(scroll)) |
                    ftxui::color(kScrollColor));
  }
  if (sf > 0) {
    parts.push_back(ftxui::text(" +" + std::to_string(sf)) |
                    ftxui::color(kSfColor));
  }
  parts.push_back(ftxui::text(") "));
  return ftxui::hbox(std::move(parts));
}

std::string InspectPanel::FormatAttackSpeed(AttackSpeed speed) {
  // Stage number matches the proto enum value (SLOWER=1 … FASTEST_3=10).
  int stage = static_cast<int>(speed);
  std::string name = AttackSpeedName(speed);
  if (name.empty()) {
    return "";
  }
  return "Stage " + std::to_string(stage) + " (" + name + ")";
}

ftxui::Element InspectPanel::JobRow(const EquipPrototype& proto, int from,
                                    int count) {
  std::set<EquipJobCategory> cats;
  for (int i = 0; i < proto.equip_job_categories_size(); ++i) {
    cats.insert(static_cast<EquipJobCategory>(proto.equip_job_categories(i)));
  }
  bool universal = cats.empty() || cats.count(EQUIP_JOB_CATEGORY_UNIVERSAL);

  std::vector<ftxui::Element> elems;
  elems.push_back(ftxui::text(" "));
  for (int i = from; i < from + count; ++i) {
    if (i > from) {
      elems.push_back(ftxui::text(" / "));
    }
    ftxui::Element name = ftxui::text(kJobCategories[i].name);
    if (!universal && !cats.count(kJobCategories[i].category)) {
      name = name | ftxui::dim;
    }
    elems.push_back(name);
  }
  elems.push_back(ftxui::text(" "));
  return ftxui::hbox(std::move(elems));
}

}  // namespace ms
