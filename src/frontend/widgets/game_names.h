/* What each proto enum value is called on screen: slots, weapon types, sets,
 * stat fields, Hyper Stats, skill kinds and Inner Ability lines. Nothing here
 * draws. Callers decide where the text goes.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_GAME_NAMES_H_
#define MS_SRC_FRONTEND_WIDGETS_GAME_NAMES_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "ftxui/screen/color.hpp"
#include "src/character/hyper_stats.h"
#include "src/character/stat_preset.h"
#include "src/item/flame.h"
#include "src/item/potential.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// A stat that can be displayed: its label and how to read it from an
// EquipStats.
struct DisplayStat {
  const char* label;
  int (EquipStats::*fn)() const;
  int GetFrom(const EquipStats& s) const {
    return (s.*fn)();
  }
};

// The display order for equip stats. Callers usually hide zero values. Change
// this array to add or reorder stats everywhere.
inline const DisplayStat kDisplayStats[] = {
    {"STR", &EquipStats::str},    {"DEX", &EquipStats::dex},
    {"INT", &EquipStats::int_},   {"LUK", &EquipStats::luk},
    {"HP", &EquipStats::max_hp},  {"MP", &EquipStats::max_mp},
    {"ATT", &EquipStats::attack}, {"MATT", &EquipStats::magic_attack},
    {"DEF", &EquipStats::def},
};

// The percentage stats only the prototype grants, in display order. Their
// rows show a % instead of a breakdown. Boss Damage isn't here: a flame adds to
// it, so the inspect card lists it with the flame's other percents.
inline const DisplayStat kDisplayPercentStats[] = {
    {"Max HP", &EquipStats::max_hp_pct},
    {"Max MP", &EquipStats::max_mp_pct},
    {"Ignore DEF", &EquipStats::ignore_enemy_defense},
    {"Item Drop Rate", &EquipStats::item_drop_rate},
};

// The kDisplayStats entry for a StatField, so a caller with a proto field (such
// as a job's primary stat) can read it from an EquipStats without its own
// switch.
const DisplayStat* DisplayStatFor(StatField field);

// The display name for an equip slot. All four ring slots read "Ring", since a
// bag row says what kind of item it is.
std::string FormatSlot(EquipSlot slot);

// The slot name with its number within the family, such as "Ring 3" or "Pendant
// 2". Used for the list of worn items, where four rings are four rows.
std::string FormatWornSlot(EquipSlot slot);

// The display name for a weapon type (e.g. "Claw"), or "" for a type with no
// name.
std::string FormatEquipType(EquipType type);

// A list of weapon types as the player reads it: "Dagger", or "Sword / Axe".
// When both hands' versions of a weapon are present they collapse to the bare
// name, since that is how the data says "any sword".
std::string FormatWeaponList(const std::vector<EquipType>& types);

// Returns the display name for a set of equipment (e.g. "Frozen Set"), or ""
// for an unnamed one.
std::string FormatEquipSet(EquipSetName set);

// The name an Inner Ability line is listed under (e.g. "Boss Damage"). The two
// Max HP lines share a name, and only the percent one shows a %.
std::string AbilityLineName(AbilityLineType type);

// What `line` is worth as the player sees it: "+40", "+20%", or "+1" for Attack
// Speed's single stage.
std::string AbilityLineValueText(const AbilityLine& line);

// The name of an Inner Ability rank, "Rare" through "Legendary", or "" for a
// preset with none.
std::string AbilityRankName(AbilityRank rank);

// The name of a potential rank, "Rare" through "Legendary", or "" for an item
// with none.
std::string PotentialRankName(PotentialRank rank);

// A cube's name and which of an item's two potentials it rerolls: "Red Cube",
// "Main".
std::string CubeName(CubeType cube);
std::string CubeTrackName(PotentialTrack track);

// The name a potential line is listed under (e.g. "Boss Damage"). A flat line
// and its percent version share a name, and the value shows the %.
std::string PotentialLineName(PotentialLineType type);

// "Magnificent Damien Soul".
std::string SoulName(const Soul& soul);
// "Boss Damage +7%, ATT +20". `magic` shows the gauge as MATT, and a flat line
// of the gauge's own stat folds into it: "ATT +30", not "ATT +10, ATT +20".
std::string SoulEffectText(const Soul& soul, bool magic);

// What `line` is worth on an item of `item_level`: "+12", "+9%", or "-2s" for a
// cooldown reduction.
std::string PotentialLineValueText(const PotentialLine& line, int item_level);

// A potential line's short name for a column: "Crit DMG", "IED", "CD". Cards
// with room use PotentialLineName.
std::string PotentialLineShortName(PotentialLineType type);

// The text for a potential column `width` wide, for a character whose primary
// stat is `primary`. It lists the effects worth most to that character, best
// first. Lines granting the same effect are summed, so two %INT lines show as
// one total. It lists as many as fit.
//
// `secondary` is the stat after the primary, shown only when the potential
// grants nothing better. An item granting nothing reads "Junk", or "-" if it
// has no potential. Values come first with no "+", since every row would carry
// one.
std::string PotentialCell(const Potential& potential, int item_level,
                          StatField primary, StatField secondary, int width);
// The effects PotentialCell picks from, best first: "Junk" or "-" alone when
// there are none.
std::vector<std::string> PotentialCellEffects(const Potential& potential,
                                              int item_level, StatField primary,
                                              StatField secondary);

// The flame column's cell, written the way PotentialCell writes its own: an
// item's lines summed into one effect each, best first, as many as fit. A pair
// counts toward both its stats. A weapon ranks ATT (MATT for a magician), Boss
// Damage, Damage, All Stats and the primary stat, and falls back to the
// secondary stat; anything else ranks the primary, All Stats, the attack and
// the secondary. "Junk" when none of those, and "-" for an item never flamed.
std::string FlameCell(const FlameLines& flame, const EquipPrototype& proto,
                      StatField primary, StatField secondary, int width);
std::vector<std::string> FlameCellEffects(const FlameLines& flame,
                                          const EquipPrototype& proto,
                                          StatField primary,
                                          StatField secondary);

// `effects` joined into a cell `width` wide: the first always, the others only
// while they fit whole, so a cut-off number never looks like a smaller one.
std::string JoinEffects(const std::vector<std::string>& effects, int width);
// The width of the first `count` of `effects` joined, or of all of them if
// there are fewer.
int JoinedEffectsWidth(const std::vector<std::string>& effects, int count);

// The tag at the start of a skill row, saying how the skill is used. Every tag
// is four columns wide, so every name starts in the same place.
struct KindTag {
  const char* text;
  ftxui::Color color;
};
constexpr int kSkillTagWidth = 4;
KindTag TagFor(const Skill& skill);

// One page of an advancement's skills in GMS's skill_order, then reordered so
// no skill comes before one it requires. What a skill does has no effect on the
// order.
//
// `hyper` picks between the book and the Hyper Skills for the same advancement.
// skill_order is unique across the two. `toggles_on` holds the toggles the
// player has switched on. A Vengeance form among them takes its Benevolence
// skill's row, and other forms are left out.
//
// The pointers are into `catalog`, which must outlive them.
std::vector<const Skill*> SkillsForAdvancement(
    const std::map<std::string, Skill>& catalog, JobAdvancement advancement,
    bool hyper = false, const std::set<std::string>& toggles_on = {});

// Every V Matrix node a character at `advancement` holds, as one list in four
// blocks: the job's own actives, the boosts for its book, its line's archetype
// nodes, then the common nodes. See VNodeSectionBreaks for where the blocks
// start. A node a boss unlocks is left out unless `defeated` names that boss.
std::vector<const Skill*> VNodesFor(const std::map<std::string, Skill>& catalog,
                                    JobAdvancement advancement,
                                    const std::set<std::string>& defeated);

// The index in VNodesFor's list of the first node of each block after the
// first, so the caller can draw a rule above each. A missing block adds no
// break.
std::vector<int> VNodeSectionBreaks(const std::vector<const Skill*>& nodes);

// The name of an attack-speed stage, "Slower" through "Fastest 3", or "" for an
// unspecified one. The stage number is the enum value, so a caller can print
// both.
std::string AttackSpeedName(AttackSpeed speed);

// True for any skill that isn't passive, attack or not. The inspect screen uses
// it for its title and to decide whether to show a damage line.
bool IsActive(const Skill& skill);

// Returns "All" for universal items or a slash-separated list of job category
// names (e.g. "Warrior/Thief"). Also returns "All" when the list is empty.
std::string FormatJobCategories(const EquipPrototype& proto);

// Returns the short display label for an AP stat field (e.g. "STR"), or "" for
// STAT_FIELD_UNSPECIFIED.
std::string StatFieldName(StatField field);

// The Hyper Stats in the order the Hyper tab lists them, which matches GMS. One
// table keeps the tab and its dialog in agreement on names and order.
extern const HyperStatField kHyperStatOrder[];
extern const int kNumHyperStats;

// What a Hyper Stat is called on screen, or "" for one with no name.
std::string HyperStatName(HyperStatField field);

// A preset slot's name. With the autoswap on, slots are named for their use.
// With it off, they are numbered. `kind` matters because gear's third slot is
// the Drop preset, while the other kinds' third slot is unnamed storage.
std::string PresetSlotName(StatPreset slot, bool autoswap,
                           PresetKind kind = PresetKind::kHyperStats);

// The chip for that name on the preset picker, with a mark on the one in use
// when the autoswap is off. The mark keeps its width either way, so a chip
// doesn't change width when it is put in use.
std::string PresetSlotLabel(StatPreset slot, bool autoswap, bool in_use,
                            PresetKind kind = PresetKind::kHyperStats);

// What `field` at `level` is worth as a row shows it: "+30" for a flat stat,
// "+3%" for a percentage, with trailing zeros trimmed. Level 0 reads "+0", so
// an empty row still shows which kind of stat it is.
std::string HyperStatBonusText(HyperStatField field, int level);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_GAME_NAMES_H_
