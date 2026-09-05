#include "src/testing/prototypes.h"

#include "src/character/skill_placement.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
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
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
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
  PlaceIn(skill, JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(1);
  skill.mutable_base()->set_damage_pct(0.075);
  skill.mutable_base()->set_final_dmg_pct(0.05);
  skill.mutable_base()->set_crit_rate(0.2);
  skill.mutable_base()->set_crit_dmg(0.025);
  skill.mutable_base()->set_attack_speed(2);
  return skill;
}

Mob SnailMob() {
  Mob mob;
  mob.set_name("Snail");
  mob.set_level(1);
  mob.set_max_hp(10);
  mob.set_exp(3);
  MobDrop* drop = mob.add_drops();
  drop->set_item("green_snail_shell");
  drop->set_per_kill(1.0);
  return mob;
}

ItemPrototype GreenSnailShell() {
  ItemPrototype item;
  item.set_name("Green Snail Shell");
  item.set_category(ITEM_CATEGORY_ETC);
  return item;
}

MapData SnailMap() {
  MapData map;
  map.set_name("Snail Field");
  Spawn* snail = map.add_spawns();
  snail->set_mob("snail");
  snail->set_count(6);
  return map;
}

Mob OgreMob() {
  Mob mob;
  mob.set_name("Ogre");
  mob.set_level(1);
  mob.set_max_hp(1000000);
  mob.set_attack(200);
  mob.set_exp(3);
  return mob;
}

MapData OgreMap() {
  MapData map;
  map.set_name("Ogre Field");
  Spawn* ogre = map.add_spawns();
  ogre->set_mob("ogre");
  ogre->set_count(1);
  return map;
}

MapData HomeMap() {
  MapData map;
  map.set_name("Maple Island");
  return map;
}

}  // namespace ms
