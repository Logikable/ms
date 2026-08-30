#include "src/testing/prototypes.h"

#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

EquipPrototype PlainSword() {
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(ATTACK_SPEED_AVERAGE);
  sword.mutable_base_stats()->set_attack(100);
  sword.mutable_base_stats()->set_magic_attack(100);
  return sword;
}

EquipPrototype IronSword() {
  EquipPrototype sword;
  sword.set_name("Iron Sword");
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.mutable_base_stats()->set_attack(30);
  sword.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  return sword;
}

EquipPrototype VanishingJourneySymbol() {
  EquipPrototype proto;
  proto.set_name("Arcane Symbol: Vanishing Journey");
  proto.set_equip_slot(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
  proto.set_required_level(200);
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  proto.add_unsupported_upgrades(UPGRADE_SCROLL);
  proto.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  proto.mutable_arcane_symbol()->set_meso_cost_base(8);
  return proto;
}

Skill IronBody() {
  Skill skill;
  skill.set_name("Iron Body");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.set_description("Boosts DEF and Max HP.");
  skill.mutable_base()->set_def(10);
  skill.mutable_base()->set_max_hp_pct(0.01);
  skill.mutable_base()->set_damage_taken_pct(0.005);
  skill.mutable_per_level()->set_def(10);
  skill.mutable_per_level()->set_max_hp_pct(0.01);
  skill.mutable_per_level()->set_damage_taken_pct(0.005);
  return skill;
}

Skill LeverPassive() {
  Skill skill;
  skill.set_name("Levers");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(1);
  skill.mutable_base()->set_damage_pct(0.075);
  skill.mutable_base()->set_final_dmg_pct(0.05);
  skill.mutable_base()->set_crit_rate(0.2);
  skill.mutable_base()->set_crit_dmg(0.025);
  skill.mutable_base()->set_attack_speed(2);
  return skill;
}

}  // namespace ms
