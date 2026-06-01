#include "src/commands/scroll.h"

#include <algorithm>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "src/character.h"
#include "src/commands/util.h"
#include "src/equip_instance.h"
#include "src/frontend.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

int PrimaryStatRank(const Scroll& scroll) {
  const EquipStats& s = scroll.stats();
  if (s.str()) {
    return 2;
  }
  if (s.dex()) {
    return 3;
  }
  if (s.int_()) {
    return 4;
  }
  if (s.luk()) {
    return 5;
  }
  if (s.max_hp()) {
    return 6;
  }
  if (s.magic_attack()) {
    return 1;
  }
  return 0;  // ATT only
}

// Returns scrolls applicable to proto: those whose applicable_job_categories
// intersects proto's equip_job_categories. Preserves input order.
std::vector<Scroll> ApplicableScrolls(const EquipPrototype& proto,
                                      const std::vector<Scroll>& scrolls) {
  std::set<int> weapon_cats(proto.equip_job_categories().begin(),
                            proto.equip_job_categories().end());
  std::vector<Scroll> result;
  for (const Scroll& scroll : scrolls) {
    for (int cat : scroll.applicable_job_categories()) {
      if (weapon_cats.count(cat)) {
        result.push_back(scroll);
        break;
      }
    }
  }
  return result;
}

std::string FormatScrollStats(const EquipStats& s) {
  std::ostringstream out;
  const char* sep = "";
  if (s.attack()) {
    out << sep << s.attack() << " ATT";
    sep = ", ";
  }
  if (s.magic_attack()) {
    out << sep << s.magic_attack() << " MATT";
    sep = ", ";
  }
  if (s.str()) {
    out << sep << s.str() << " STR";
    sep = ", ";
  }
  if (s.dex()) {
    out << sep << s.dex() << " DEX";
    sep = ", ";
  }
  if (s.int_()) {
    out << sep << s.int_() << " INT";
    sep = ", ";
  }
  if (s.luk()) {
    out << sep << s.luk() << " LUK";
    sep = ", ";
  }
  if (s.max_hp()) {
    out << sep << s.max_hp() << " HP";
    sep = ", ";
  }
  if (s.def()) {
    out << sep << s.def() << " DEF";
    sep = ", ";
  }
  return out.str();
}

}  // namespace

void SortScrolls(std::vector<Scroll>& scrolls) {
  std::sort(scrolls.begin(), scrolls.end(),
            [](const Scroll& a, const Scroll& b) {
              int ra = PrimaryStatRank(a);
              int rb = PrimaryStatRank(b);
              if (ra != rb) {
                return ra < rb;
              }
              return a.success_rate() > b.success_rate();
            });
}

std::string ScrollListCommand(const CharacterInstance& character,
                              const std::vector<Scroll>& scrolls) {
  if (!character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON)) {
    return "No weapon equipped.";
  }
  const EquipInstance& item =
      character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON);
  std::vector<Scroll> applicable = ApplicableScrolls(item.prototype(), scrolls);
  if (applicable.empty()) {
    return "No applicable scrolls for " + item.prototype().name() + ".";
  }
  size_t max_name = 0;
  for (const Scroll& s : applicable) {
    if (s.name().size() > max_name) {
      max_name = s.name().size();
    }
  }
  std::ostringstream out;
  out << item.prototype().name() << " ["
      << item.proto().remaining_upgrade_slots() << " upgrade slots]\n";
  for (int i = 0; i < static_cast<int>(applicable.size()); ++i) {
    const Scroll& s = applicable[i];
    out << "[" << i << "] " << s.name()
        << std::string(max_name - s.name().size() + 2, ' ')
        << FormatScrollStats(s.stats()) << "\n";
  }
  return out.str();
}

std::string ScrollApplyCommand(CharacterInstance& character,
                               const std::vector<Scroll>& scrolls, int index,
                               std::mt19937& rng) {
  if (!character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON)) {
    return "No weapon equipped.";
  }
  const EquipInstance& item =
      character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON);
  if (item.proto().remaining_upgrade_slots() == 0) {
    return "No upgrade slots remaining on " + item.prototype().name() + ".";
  }
  std::vector<Scroll> applicable = ApplicableScrolls(item.prototype(), scrolls);
  if (index < 0 || index >= static_cast<int>(applicable.size())) {
    return "Invalid scroll index.";
  }
  bool success = character.ScrollEquipped(EQUIP_SLOT_PRIMARY_WEAPON,
                                          applicable[index], rng);
  std::ostringstream out;
  out << (success ? "Success! " : "Failed.  ");
  out << FormatEquip(character.equipped().at(EQUIP_SLOT_PRIMARY_WEAPON));
  return out.str();
}

void RegisterScrollCommand(Frontend& frontend, CharacterInstance& character,
                           const std::vector<Scroll>& scrolls,
                           std::mt19937& rng) {
  frontend.Register({
      "scroll",
      "List or apply scrolls for the equipped primary weapon.",
      [&character, &scrolls,
       &rng](std::vector<std::string> tokens) -> std::string {
        if (tokens.size() < 2) {
          return ScrollListCommand(character, scrolls);
        }
        for (char c : tokens[1]) {
          if (!std::isdigit(static_cast<unsigned char>(c))) {
            return "Usage: /scroll [index]";
          }
        }
        return ScrollApplyCommand(character, scrolls, std::stoi(tokens[1]),
                                  rng);
      },
  });
}

}  // namespace ms
