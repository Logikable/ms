#include "src/character/set_pieces.h"

#include <iterator>
#include <string>
#include <vector>

#include "src/character/job_branch.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

std::vector<std::string> RootAbyssArmour(Job job) {
  switch (BranchOf(job)) {
    case JobBranch::kWarrior:
      return {"royal_warrior_helm", "eagle_eye_warrior_armor",
              "trixter_warrior_pants"};
    case JobBranch::kArcher:
      return {"royal_ranger_beret", "eagle_eye_ranger_cowl",
              "trixter_ranger_pants"};
    case JobBranch::kMagician:
      return {"royal_dunwitch_hat", "eagle_eye_dunwitch_robe",
              "trixter_dunwitch_pants"};
    case JobBranch::kRogue:
      return {"royal_assassin_hood", "eagle_eye_assassin_shirt",
              "trixter_assassin_pants"};
    default:
      return {};
  }
}

std::string RootAbyssWeapon(Job job) {
  switch (job) {
    case JOB_HERO:
      return "fafnir_battle_cleaver";
    case JOB_PALADIN:
      return "fafnir_lightning_striker";
    case JOB_DARK_KNIGHT:
      return "fafnir_brionak";
    case JOB_BOW_MASTER:
      return "fafnir_wind_chaser";
    case JOB_MARKSMAN:
      return "fafnir_windwing_shooter";
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
    case JOB_FIRE_POISON_ARCH_MAGE:
    case JOB_BISHOP:
      return "fafnir_mana_crown";
    case JOB_NIGHT_LORD:
      return "fafnir_risk_holder";
    case JOB_SHADOWER:
      return "fafnir_damascus";
    default:
      return "";
  }
}

std::vector<std::string> PrincessNoSecondary(Job job) {
  switch (job) {
    case JOB_HERO:
      return {"princess_nos_medal"};
    case JOB_PALADIN:
      return {"princess_nos_rosary"};
    case JOB_DARK_KNIGHT:
      return {"princess_nos_flower_chain"};
    case JOB_FIRE_POISON_ARCH_MAGE:
      return {"princess_nos_flaming_book"};
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
      return {"princess_nos_damp_book"};
    case JOB_BISHOP:
      return {"princess_nos_golden_book"};
    case JOB_BOW_MASTER:
      return {"princess_nos_feather"};
    case JOB_MARKSMAN:
      return {"princess_nos_wreath"};
    case JOB_NIGHT_LORD:
      return {"princess_nos_charm"};
    case JOB_SHADOWER:
      return {"princess_nos_purple_shadow"};
    default:
      return {};
  }
}

std::string AbsoLabWeapon(Job job) {
  switch (job) {
    case JOB_HERO:
      return "absolab_broad_axe";
    case JOB_PALADIN:
      return "absolab_broad_hammer";
    case JOB_DARK_KNIGHT:
      return "absolab_piercing_spear";
    case JOB_BOW_MASTER:
      return "absolab_sureshot_bow";
    case JOB_MARKSMAN:
      return "absolab_crossbow";
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
    case JOB_FIRE_POISON_ARCH_MAGE:
    case JOB_BISHOP:
      return "absolab_spellsong_staff";
    case JOB_NIGHT_LORD:
      return "absolab_revenge_guard";
    case JOB_SHADOWER:
      return "absolab_blade_lord";
    default:
      return "";
  }
}

std::vector<std::string> AbsoLabArmour(Job job) {
  switch (BranchOf(job)) {
    case JobBranch::kWarrior:
      return {"absolab_knight_helm",    "absolab_knight_armor",
              "absolab_knight_pants",   "absolab_knight_shoes",
              "absolab_knight_gloves",  "absolab_knight_cape",
              "absolab_knight_shoulder"};
    case JobBranch::kArcher:
      return {"absolab_archer_hood",    "absolab_archer_armor",
              "absolab_archer_pants",   "absolab_archer_shoes",
              "absolab_archer_gloves",  "absolab_archer_cape",
              "absolab_archer_shoulder"};
    case JobBranch::kMagician:
      return {"absolab_mage_crown",   "absolab_mage_armor",
              "absolab_mage_pants",   "absolab_mage_shoes",
              "absolab_mage_gloves",  "absolab_mage_cape",
              "absolab_mage_shoulder"};
    case JobBranch::kRogue:
      return {"absolab_bandit_cap",     "absolab_bandit_armor",
              "absolab_bandit_pants",   "absolab_bandit_shoes",
              "absolab_bandit_gloves",  "absolab_bandit_cape",
              "absolab_bandit_shoulder"};
    default:
      return {};
  }
}

std::string ArcaneUmbraWeapon(Job job) {
  switch (job) {
    case JOB_HERO:
      return "arcane_umbra_two_handed_axe";
    case JOB_PALADIN:
      return "arcane_umbra_two_handed_hammer";
    case JOB_DARK_KNIGHT:
      return "arcane_umbra_spear";
    case JOB_BOW_MASTER:
      return "arcane_umbra_bow";
    case JOB_MARKSMAN:
      return "arcane_umbra_crossbow";
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
    case JOB_FIRE_POISON_ARCH_MAGE:
    case JOB_BISHOP:
      return "arcane_umbra_staff";
    case JOB_NIGHT_LORD:
      return "arcane_umbra_guards";
    case JOB_SHADOWER:
      return "arcane_umbra_dagger";
    default:
      return "";
  }
}

std::vector<std::string> ArcaneUmbraArmour(Job job) {
  std::string branch;
  switch (BranchOf(job)) {
    case JobBranch::kWarrior:
      branch = "knight";
      break;
    case JobBranch::kArcher:
      branch = "archer";
      break;
    case JobBranch::kMagician:
      branch = "mage";
      break;
    case JobBranch::kRogue:
      branch = "thief";
      break;
    default:
      return {};
  }
  std::vector<std::string> names;
  for (const char* piece :
       {"hat", "suit", "pants", "shoes", "gloves", "cape", "shoulder"}) {
    names.push_back("arcane_umbra_" + branch + "_" + piece);
  }
  return names;
}

std::string CygnusShoulder(Job job) {
  switch (BranchOf(job)) {
    case JobBranch::kWarrior:
      return "lionheart_battle_shoulder";
    case JobBranch::kArcher:
      return "falcon_wing_sentinel_shoulder";
    case JobBranch::kMagician:
      return "dragon_tail_mage_shoulder";
    case JobBranch::kRogue:
      return "raven_horn_chaser_shoulder";
    default:
      return "";
  }
}

namespace {

// The slots AbsoLabArmour and ArcaneUmbraArmour list, in their order. Root
// Abyss lists the first three.
constexpr EquipSlot kArmourSlots[] = {
    EQUIP_SLOT_HAT,    EQUIP_SLOT_TOP,  EQUIP_SLOT_BOTTOM,  EQUIP_SLOT_SHOES,
    EQUIP_SLOT_GLOVES, EQUIP_SLOT_CAPE, EQUIP_SLOT_SHOULDER};

std::string ArmourPiece(const std::vector<std::string>& armour,
                        EquipSlot slot) {
  for (size_t i = 0; i < armour.size() && i < std::size(kArmourSlots); ++i) {
    if (kArmourSlots[i] == slot) {
      return armour[i];
    }
  }
  return "";
}

}  // namespace

std::string SetPieceFor(SetFamily family, EquipSlot slot, Job job) {
  switch (family) {
    case SetFamily::kRootAbyss:
      return slot == EQUIP_SLOT_PRIMARY_WEAPON
                 ? RootAbyssWeapon(job)
                 : ArmourPiece(RootAbyssArmour(job), slot);
    case SetFamily::kPrincessNo: {
      const std::vector<std::string> secondary = PrincessNoSecondary(job);
      return slot == EQUIP_SLOT_SECONDARY && !secondary.empty() ? secondary[0]
                                                                : "";
    }
    case SetFamily::kAbsoLab:
      return slot == EQUIP_SLOT_PRIMARY_WEAPON
                 ? AbsoLabWeapon(job)
                 : ArmourPiece(AbsoLabArmour(job), slot);
    case SetFamily::kArcaneUmbra:
      return slot == EQUIP_SLOT_PRIMARY_WEAPON
                 ? ArcaneUmbraWeapon(job)
                 : ArmourPiece(ArcaneUmbraArmour(job), slot);
    case SetFamily::kCygnus:
      return slot == EQUIP_SLOT_SHOULDER ? CygnusShoulder(job) : "";
  }
  return "";
}

}  // namespace ms
