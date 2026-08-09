/* Prices spell trace scrolls against what a player can actually afford.
 *
 * Two jobs a player does, and this reports both:
 *
 *   - 100% a fresh weapon every 20-30 levels. Every slot succeeds, so the bill
 *     is just slots x price. What varies is the money earned since the last
 *     weapon, which runs from 391k at level 40 to 19M at 140 -- which is why
 *     the price has to be per tier and not flat.
 *
 *   - Fully scroll an endgame weapon with 15% scrolls and clean slates. Clean
 *     Slate here is 100%, not GMS's 20%, so a failed slot is always bought
 *     back and the whole thing is a deterministic grind rather than a gamble.
 *     Per slot that is 1/p attempts and (1/p - 1) / p_clean_slate slates.
 *
 * Both are read against //analysis:meso_curve, so a change to the economy
 * moves the prices here with it. Slot counts, success rates and tiers come
 * from the shipped catalogs, not from constants.
 *
 * Scratch analysis tool, not part of the game.
 */
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "analysis/meso_curve.h"
#include "analysis/sim_format.h"
#include "src/embedded_data.h"
#include "src/item/equip_instance.h"
#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

ABSL_FLAG(double, trace_meso, 5000.0, "Meso one spell trace costs.");
ABSL_FLAG(double, clean_slate, 600.0, "Traces one clean slate costs.");
ABSL_FLAG(double, fifteen, 200.0, "Traces one 15% scroll costs.");
ABSL_FLAG(std::string, hundred, "3,50,80",
          "Traces one 100% scroll costs, by tier: T1,T2,T3.");
ABSL_FLAG(std::string, upgrades, "40,65,90,115,140",
          "Levels at which the player 100%s a fresh weapon.");
ABSL_FLAG(std::string, endgame_equip, "fafnir_mistilteinn",
          "Data file stem of the weapon scrolled with 15% scrolls.");
ABSL_FLAG(int, endgame_by, 210,
          "Level the endgame weapon must be finished by.");
ABSL_FLAG(int, hammers, 2,
          "Extra slots golden hammers add. NOTE: hammers are not implemented, "
          "and the clean slate cap in equip_instance.cc counts the "
          "prototype's slots, so hammer slots cannot be bought back yet.");
ABSL_FLAG(double, target_share, 0.20,
          "Share of the money earned since the last upgrade that one job "
          "should cost. The solved prices aim at this.");
ABSL_FLAG(int, etc_stops_at, 200, "Passed through to the meso curve.");

namespace {

// The scroll rates the catalog actually ships, so this cannot drift from the
// data. Rates are whole percents.
struct Rates {
  double fifteen = 0.0;
  double clean_slate = 0.0;
};

Rates ReadRates(const std::map<std::string, ms::Scroll>& scrolls) {
  Rates rates;
  for (std::map<std::string, ms::Scroll>::const_iterator it = scrolls.begin();
       it != scrolls.end(); ++it) {
    const ms::Scroll& scroll = it->second;
    if (scroll.scroll_category() == ms::SCROLL_CATEGORY_CLEAN_SLATE) {
      rates.clean_slate = scroll.success_rate() / 100.0;
    } else if (scroll.success_rate() == 15) {
      rates.fifteen = 0.15;
    }
  }
  if (rates.fifteen <= 0.0 || rates.clean_slate <= 0.0) {
    LOG(FATAL) << "the scroll catalog has no 15% scroll or no clean slate";
  }
  return rates;
}

const ms::EquipPrototype& FindEquip(
    const std::map<std::string, ms::EquipPrototype>& equips,
    const std::string& stem) {
  std::map<std::string, ms::EquipPrototype>::const_iterator it =
      equips.find(stem);
  if (it == equips.end()) {
    LOG(FATAL) << "no equipment named " << stem << " in data/equip";
  }
  return it->second;
}

// Slots per weapon at each upgrade level. Every weapon in the catalog carries
// the same count, so the median is the whole story; taking it from the data
// keeps this honest if one ever differs.
int TypicalWeaponSlots(
    const std::map<std::string, ms::EquipPrototype>& equips) {
  std::map<int, int> counts;
  for (std::map<std::string, ms::EquipPrototype>::const_iterator it =
           equips.begin();
       it != equips.end(); ++it) {
    if (it->second.upgrade_slots() > 0) {
      ++counts[it->second.upgrade_slots()];
    }
  }
  int best = 0;
  int best_count = 0;
  for (std::map<int, int>::const_iterator it = counts.begin();
       it != counts.end(); ++it) {
    if (it->second > best_count) {
      best = it->first;
      best_count = it->second;
    }
  }
  return best;
}

std::vector<double> ParsePrices(const std::string& spec) {
  std::vector<int> parsed = ms::ParseLevels(spec, "--hundred");
  if (parsed.size() != 3) {
    LOG(FATAL) << "--hundred needs three prices, one per tier";
  }
  std::vector<double> prices;
  for (int i = 0; i < 3; ++i) {
    prices.push_back(parsed[i]);
  }
  return prices;
}

// The 100% job: every slot succeeds, so the bill is slots x price. Reported
// against the money earned since the previous weapon, which is what pays.
void PrintHundredJobs(const ms::MesoCurve& curve,
                      const std::vector<double>& prices, int slots) {
  double trace = absl::GetFlag(FLAGS_trace_meso);
  double target = absl::GetFlag(FLAGS_target_share);
  std::vector<int> levels =
      ms::ParseLevels(absl::GetFlag(FLAGS_upgrades), "--upgrades");
  printf("\n100%% a fresh %d-slot weapon at each upgrade\n", slots);
  char solved_header[24];
  snprintf(solved_header, sizeof(solved_header), "for %.0f%%", 100.0 * target);
  printf("%6s %5s %7s %14s %14s %8s %10s\n", "level", "tier", "price",
         "job cost", "earned since", "share", solved_header);
  int previous = 1;
  for (int i = 0; i < static_cast<int>(levels.size()); ++i) {
    int level = levels[i];
    ms::ScrollTier tier = ms::TierForLevel(level);
    double price = prices[static_cast<int>(tier) - 1];
    double cost = slots * price * trace;
    double earned = curve.Earned(previous, level);
    char cost_text[32];
    char earned_text[32];
    ms::FormatShort(cost, cost_text, sizeof(cost_text));
    ms::FormatShort(earned, earned_text, sizeof(earned_text));
    // What the price would have to be to land exactly on --target_share.
    double solved = earned > 0.0 ? target * earned / (slots * trace) : 0.0;
    printf("%6d %5d %7.0f %14s %14s %7.1f%% %10.1f\n", level,
           static_cast<int>(tier), price, cost_text, earned_text,
           earned > 0.0 ? 100.0 * cost / earned : 0.0, solved);
    previous = level;
  }
  printf(
      "  last column: the price that would make that job cost %.0f%% of "
      "the band\n",
      100.0 * target);
}

// The 15% job. Clean Slate buys the slot back, so the only question is how
// many of each the player gets through.
void PrintFifteenJob(const ms::MesoCurve& curve,
                     const ms::EquipPrototype& equip, const Rates& rates) {
  double trace = absl::GetFlag(FLAGS_trace_meso);
  int hammers = absl::GetFlag(FLAGS_hammers);
  int slots = equip.upgrade_slots() + hammers;
  int by = absl::GetFlag(FLAGS_endgame_by);
  double earned = curve.Earned(equip.required_level(), by);

  double attempts = 1.0 / rates.fifteen;
  double slates = (attempts - 1.0) / rates.clean_slate;
  printf("\nfully scroll %s at 15%%: %d slots (%d + %d hammer), Lv%d to %d\n",
         equip.name().c_str(), slots, equip.upgrade_slots(), hammers,
         equip.required_level(), by);
  printf("  clean slate is %.0f%%, so a failed slot always comes back\n",
         100.0 * rates.clean_slate);
  printf("  per slot: %.2f fifteens + %.2f clean slates\n", attempts, slates);
  char earned_text[32];
  ms::FormatShort(earned, earned_text, sizeof(earned_text));
  printf("  earned across that window: %s meso\n\n", earned_text);

  printf("  %8s %8s %14s %14s %14s %8s %9s\n", "15% ea", "slate ea", "scrolls",
         "slates", "total", "of window", "slate cut");
  const double kSweep[] = {100.0, 200.0, 300.0, 500.0};
  double chosen_fifteen = absl::GetFlag(FLAGS_fifteen);
  double chosen_slate = absl::GetFlag(FLAGS_clean_slate);
  for (int i = 0; i < 5; ++i) {
    double fifteen = i == 0 ? chosen_fifteen : kSweep[i - 1];
    if (i > 0 && fifteen == chosen_fifteen) {
      continue;  // already shown as the chosen row
    }
    double slate = chosen_slate;
    double scroll_cost = slots * attempts * fifteen * trace;
    double slate_cost = slots * slates * slate * trace;
    double total = scroll_cost + slate_cost;
    char a[32];
    char b[32];
    char c[32];
    ms::FormatShort(scroll_cost, a, sizeof(a));
    ms::FormatShort(slate_cost, b, sizeof(b));
    ms::FormatShort(total, c, sizeof(c));
    printf("  %8.0f %8.0f %14s %14s %14s %7.1f%% %8.0f%%%s\n", fifteen, slate,
           a, b, c, earned > 0.0 ? 100.0 * total / earned : 0.0,
           total > 0.0 ? 100.0 * slate_cost / total : 0.0,
           i == 0 ? "   <- chosen" : "");
  }

  // The clean slate is bought almost as often as the scroll, so the two prices
  // trade off nearly one for one. Saying what the slates alone cost is what
  // shows when the scroll price has stopped being a lever.
  double slate_only = slots * slates * chosen_slate * trace;
  char text[32];
  ms::FormatShort(slate_only, text, sizeof(text));
  printf(
      "\n  clean slates alone at %.0f traces: %s meso, %.1f%% of the "
      "window\n",
      chosen_slate, text, earned > 0.0 ? 100.0 * slate_only / earned : 0.0);
  printf(
      "  a scroll and a slate are bought %.2f to %.2f, so their prices "
      "trade off\n  nearly one for one -- the sum is the real lever, the "
      "split is flavour\n",
      attempts, slates);
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  std::map<std::string, ms::EquipPrototype> equips =
      ms::LoadTextProtoMap<ms::EquipPrototype>(ms::EmbeddedEquips());
  std::map<std::string, ms::Scroll> scrolls =
      ms::LoadTextProtoMap<ms::Scroll>(ms::EmbeddedScrolls());

  ms::MesoCurveParams params;
  params.etc_stops_at = absl::GetFlag(FLAGS_etc_stops_at);
  ms::MesoCurve curve = ms::BuildMesoCurve(params);

  printf("a spell trace is %.0f meso\n", absl::GetFlag(FLAGS_trace_meso));
  PrintHundredJobs(curve, ParsePrices(absl::GetFlag(FLAGS_hundred)),
                   TypicalWeaponSlots(equips));
  PrintFifteenJob(curve, FindEquip(equips, absl::GetFlag(FLAGS_endgame_equip)),
                  ReadRates(scrolls));
  return 0;
}
