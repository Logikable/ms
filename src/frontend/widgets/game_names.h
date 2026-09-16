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
#include "src/character/stat_preset.h"
#include "src/item/potential.h"
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

// The kDisplayStats entry a StatField names, so a caller holding a proto field
// -- a job's primary stat -- can read it off an EquipStats without a switch of
// its own.
const DisplayStat* DisplayStatFor(StatField field);

// The display name for an equip slot. A ring reads "Ring" whichever of the
// four it names: what a bag row asks is what KIND of thing this is.
std::string FormatSlot(EquipSlot slot);

// The same name, saying which slot of its family this is: "Ring 3",
// "Pendant 2". For a list of what is worn, where four rings are four rows.
std::string FormatWornSlot(EquipSlot slot);

// Returns the display name for a weapon type (e.g. "Claw"). Returns "" for
// types not yet implemented.
std::string FormatEquipType(EquipType type);

// A list of weapon types as the player reads it: "Dagger", or "Sword / Axe".
// Both hands' versions of one weapon COLLAPSE to the bare name -- that is how
// the data says "any sword", not how it should be shown.
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

// The rank an Inner Ability reads as: "Rare" through "Legendary", and "" for
// a preset carrying none.
std::string AbilityRankName(AbilityRank rank);

// The rank a potential reads as: "Rare" through "Legendary", and "" for an
// item carrying none.
std::string PotentialRankName(PotentialRank rank);

// What a cube is called on the shelf, and which of an item's two potentials
// it rerolls: "Red Cube", "Main".
std::string CubeName(CubeType cube);
std::string CubeTrackName(PotentialTrack track);

// The name a potential line is listed under (e.g. "Boss Damage"). A flat line
// and its percent twin share one: the value beside it carries the %.
std::string PotentialLineName(PotentialLineType type);

// What `line` is worth on an item of `item_level`, as the player reads it:
// "+12", "+9%", and "-2s" for the seconds a cooldown line takes off.
std::string PotentialLineValueText(const PotentialLine& line, int item_level);

// The name a potential line goes by where there is only a column for it:
// "Crit DMG", "IED", "CD". A card with room asks PotentialLineName.
std::string PotentialLineShortName(PotentialLineType type);

// What a list column says about `potential` for a character built on
// `primary`: the one effect worth the most to them, with every other line
// granting it folded in, so two %INT lines read as one total. An item granting
// none reads "-". Value first and no "+" -- a column has no room for a sign
// every row carries -- and held to kPotentialCellWidth.
std::string PotentialCell(const Potential& potential, int item_level,
                          StatField primary);
inline constexpr int kPotentialCellWidth = 12;

// The tag a skill row opens with: what the player does with the skill, said at
// the front rather than worked out from the name. FOUR columns whichever tag
// it is, so every name starts in the same place.
struct KindTag {
  const char* text;
  ftxui::Color color;
};
constexpr int kSkillTagWidth = 4;
KindTag TagFor(const Skill& skill);

// One page of an advancement's skills, in GMS's own skill_order, then settled
// so nothing waits on a skill listed below it. What a skill DOES has no say: a
// second rule would only fight skill_order.
//
// `hyper` picks the book or the Hyper Skills naming the same advancement --
// two lists, so skill_order is distinct within the PAIR. `toggles_on` is the
// toggles the reader has switched on: a Vengeance form among them stands in
// its Benevolence skill's row, and every other form is left out.
//
// The pointers are into `catalog`, which must outlive them.
std::vector<const Skill*> SkillsForAdvancement(
    const std::map<std::string, Skill>& catalog, JobAdvancement advancement,
    bool hyper = false, const std::set<std::string>& toggles_on = {});

// Every V Matrix node a character at `advancement` holds. ONE list, the matrix
// being one page, in four blocks: the job's own actives, the boosts lifting
// its book, its line's archetype nodes, then the commons. What is the
// character's own leads. See VNodeSectionBreaks for the divisions.
std::vector<const Skill*> VNodesFor(const std::map<std::string, Skill>& catalog,
                                    JobAdvancement advancement);

// Where a V page breaks into sections: the index of the FIRST node of each
// block after the head one. A page missing a block reports no break there.
// Indices into VNodesFor's list, so the caller draws a rule above each.
std::vector<int> VNodeSectionBreaks(const std::vector<const Skill*>& nodes);

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

// What a preset slot is called: named for what it is for with the autoswap on,
// numbered with it off. `kind` is asked because gear's third slot is the Drop
// preset where the other kinds' third is unnamed storage.
std::string PresetSlotName(StatPreset slot, bool autoswap,
                           PresetKind kind = PresetKind::kHyperStats);

// The chip that name draws as on the row that picks between them, where the
// one in use carries a mark. The mark keeps its column either way, so a chip
// never changes width as one is put in use.
std::string PresetSlotLabel(StatPreset slot, bool autoswap, bool in_use,
                            PresetKind kind = PresetKind::kHyperStats);

// What `field` at `level` is worth, written the way a row shows it: "+30" for
// a flat stat and "+3%" for a percentage, trailing zeros trimmed. Level 0
// reads "+0", so an untouched row still says which kind of stat it is.
std::string HyperStatBonusText(HyperStatField field, int level);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_GAME_NAMES_H_
