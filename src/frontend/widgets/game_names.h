/* What a proto enum is called on screen.
 *
 * One place for every "which string does this value read as" question the
 * panels ask -- slots, weapon types, sets, stat fields, hyper stats, skill
 * kinds, Inner Ability lines. Nothing here draws anything: a caller gets the
 * text and decides where it goes.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_GAME_NAMES_H_
#define MS_SRC_FRONTEND_WIDGETS_GAME_NAMES_H_

#include <map>
#include <string>
#include <vector>

#include "ftxui/screen/color.hpp"
#include "src/character/hyper_stats.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// A single displayable stat: its label and how to read it from an EquipStats.
struct DisplayStat {
  const char* label;
  int (EquipStats::*fn)() const;
  int GetFrom(const EquipStats& s) const {
    return (s.*fn)();
  }
};

// Canonical display order for equip stats. Zero-value fields are typically
// hidden by callers. Update this array to add or reorder stats site-wide.
inline const DisplayStat kDisplayStats[] = {
    {"STR", &EquipStats::str},    {"DEX", &EquipStats::dex},
    {"INT", &EquipStats::int_},   {"LUK", &EquipStats::luk},
    {"HP", &EquipStats::max_hp},  {"MP", &EquipStats::max_mp},
    {"ATT", &EquipStats::attack}, {"MATT", &EquipStats::magic_attack},
    {"DEF", &EquipStats::def},
};

// The percentage stats an equip can carry, in display order. Held apart from
// kDisplayStats because a scroll and a star never grant one: these read off
// the prototype alone, and a row for one carries a % rather than a breakdown.
inline const DisplayStat kDisplayPercentStats[] = {
    {"Max HP", &EquipStats::max_hp_pct},
    {"Max MP", &EquipStats::max_mp_pct},
    {"Boss Damage", &EquipStats::boss_damage},
    {"Ignore DEF", &EquipStats::ignore_enemy_defense},
    {"Item Drop Rate", &EquipStats::item_drop_rate},
};

// The kDisplayStats entry a StatField names, or nullptr for a field with no
// equip stat behind it. Lets a caller that knows a stat by its proto field --
// a job's primary stat, say -- read it off an EquipStats without writing its
// own switch over the four stats.
const DisplayStat* DisplayStatFor(StatField field);

// Returns the display name for an equip slot (e.g. "Weapon"). A ring reads
// "Ring" whichever of the four slots it names, because this is the question a
// bag row and a set's piece list ask: what kind of thing is this. Returns ""
// for slot types not yet implemented.
std::string FormatSlot(EquipSlot slot);

// The same name, saying which slot of its family this is: "Ring 3",
// "Pendant 2". For a list of what is worn, where four rings are four rows.
std::string FormatWornSlot(EquipSlot slot);

// Returns the display name for a weapon type (e.g. "Claw"). Returns "" for
// types not yet implemented.
std::string FormatEquipType(EquipType type);

// A list of weapon types as the player reads it: "Dagger", or "Sword / Axe".
// Both hands' versions of one weapon collapse to the bare name, which is how
// the data says "any sword" and not how it should be shown. A pair lands where
// its first half was listed. Empty in, empty out.
std::string FormatWeaponList(const std::vector<EquipType>& types);

// Returns the display name for a set of equipment (e.g. "Frozen Set"), or ""
// for an unnamed one.
std::string FormatEquipSet(EquipSetName set);

// The name an Inner Ability line is listed under (e.g. "Boss Damage"). The
// two Max HP lines share one: the value beside it says which, since only the
// percent one carries a %.
std::string AbilityLineName(AbilityLineType type);

// What `line` is worth, as the player reads it: "+40", "+20%", and "+1" for
// the single swing stage Attack Speed grants.
std::string AbilityLineValueText(const AbilityLine& line);

// The rank a potential reads as: "Rare" through "Legendary", and "" for an
// item carrying none.
std::string PotentialRankName(PotentialRank rank);

// The name a potential line is listed under (e.g. "Boss Damage"). A flat line
// and its percent twin share one: the value beside it carries the %.
std::string PotentialLineName(PotentialLineType type);

// What `line` is worth on an item of `item_level`, as the player reads it:
// "+12", "+9%", and "-2s" for the seconds a cooldown line takes off.
std::string PotentialLineValueText(const PotentialLine& line, int item_level);

// The tag a skill row opens with: what the player does with the skill, said
// once at the front of the row instead of being worked out from the name.
// Four columns wide whichever tag it is, so every name after it starts at the
// same place. A kind-less skill gets the blanks rather than a wrong tag.
struct KindTag {
  const char* text;
  ftxui::Color color;
};
constexpr int kSkillTagWidth = 4;
KindTag TagFor(const Skill& skill);

// One page of an advancement's skills out of `catalog`, in the order every
// page lists them: GMS's own skill_order, then settled so nothing waits on a
// skill listed below it. What a skill does has no say -- the wiki does not
// gather the attacks above the passives, and a second rule would only fight
// skill_order.
//
// `hyper` picks which of the advancement's two pages: its book, or the Hyper
// Skills that name the same advancement. They are two lists rather than one,
// so skill_order is distinct within the PAIR.
//
// `toggles_on` is the display names of the toggle skills the reader has
// switched on -- Character.active_skill. A Vengeance form whose toggle is
// among them stands in the row of the Benevolence skill it replaces; every
// other form is left out entirely, the two being one row of the book. An
// empty set is the book as it is written, which is what a page with no reader
// wants.
//
// The pointers are into `catalog`, which has to outlive them. Empty for an
// unspecified advancement, so a caller may pass one straight through.
std::vector<const Skill*> SkillsForAdvancement(
    const std::map<std::string, Skill>& catalog, JobAdvancement advancement,
    bool hyper = false, const std::set<std::string>& toggles_on = {});

// The name of an attack-speed stage, "Slower" through "Fastest 3", or "" for
// an unspecified one. The stage number is the proto enum's own value, so a
// caller wanting both can print it beside this.
std::string AttackSpeedName(AttackSpeed speed);

// True for a skill the player casts, attack or otherwise -- everything that
// isn't a passive. It is what the inspect screen titles itself with, and what
// decides whether a skill's damage line is worth showing at all.
bool IsActive(const Skill& skill);

// Returns "All" for universal items or a slash-separated list of job category
// names (e.g. "Warrior/Thief"). Also returns "All" when the list is empty.
std::string FormatJobCategories(const EquipPrototype& proto);

// Returns the short display label for an AP stat field (e.g. "STR"), or "" for
// STAT_FIELD_UNSPECIFIED.
std::string StatFieldName(StatField field);

// The Hyper Stats in the order the Hyper tab lists them, which is the order
// GMS lists them in. One table so the tab and the dialog behind it cannot
// disagree about what a stat is called or where it sits.
extern const HyperStatField kHyperStatOrder[];
extern const int kNumHyperStats;

// What a Hyper Stat is called on screen, or "" for one with no name.
std::string HyperStatName(HyperStatField field);

// What `field` at `level` is worth, written the way a row shows it: "+30" for
// a flat stat and "+3%" for a percentage, trailing zeros trimmed. Level 0
// reads "+0", so an untouched row still says which kind of stat it is.
std::string HyperStatBonusText(HyperStatField field, int level);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_GAME_NAMES_H_
