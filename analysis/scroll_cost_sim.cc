/* What GMS's own spell trace prices cost a player in this game's economy.
 *
 * The trace costs are GMS's (src/item/spell_trace_cost.h). What this adds is
 * the two things GMS cannot tell us: how many scrolls a job actually takes,
 * and whether the player has the meso for it by the level they want it done.
 *
 * Two ways to scroll, and the sim reports both:
 *
 *   - One pass, one scroll per slot. What a player spends in a sitting. At
 *     100% every slot lands; at 70% and 30% most of the slots are simply lost,
 *     so the useful number is the cost per slot that stuck.
 *   - Fill every slot, buying failures back with clean slates. Clean Slate
 *     here is 100%, not GMS's 20%, so this always terminates: 1/p scrolls and
 *     (1/p - 1) / p_slate slates a slot.
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
#include "src/item/spell_trace_cost.h"
#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

ABSL_FLAG(double, trace_meso, 5000.0, "Meso one spell trace costs.");
ABSL_FLAG(std::string, weapons, "40,70,100,140",
          "Weapon levels to scroll with 100/70/30.");
ABSL_FLAG(int, slots, 7,
          "Upgrade slots on those weapons. Every weapon in data/equip has 7. "
          "No golden hammer is assumed.");
ABSL_FLAG(std::string, fifteen_equip, "fafnir_mistilteinn",
          "Data file stem of the weapon scrolled at 15%.");
ABSL_FLAG(int, hammers, 2,
          "Extra slots golden hammers would add to the 15% weapon. NOTE: "
          "hammers are not implemented, and the clean slate cap in "
          "equip_instance.cc counts the prototype's slots, so a hammer slot "
          "could not be bought back yet.");
ABSL_FLAG(int, etc_stops_at, 200, "Passed through to the meso curve.");

namespace {

const int kRates[] = {100, 70, 30};

double TraceMeso() {
  return absl::GetFlag(FLAGS_trace_meso);
}

int WeaponCost(int level, int rate) {
  return ms::SpellTraceCost(level, ms::TraceCategory::kWeapon, rate);
}

// The clean slate rate the catalog actually ships, so this cannot drift from
// the data.
double CleanSlateRate(const std::map<std::string, ms::Scroll>& scrolls) {
  for (std::map<std::string, ms::Scroll>::const_iterator it = scrolls.begin();
       it != scrolls.end(); ++it) {
    if (it->second.scroll_category() == ms::SCROLL_CATEGORY_CLEAN_SLATE) {
      return it->second.success_rate() / 100.0;
    }
  }
  LOG(FATAL) << "the scroll catalog has no clean slate";
}

void PrintPriceTable(const std::vector<int>& levels, int fifteen_level) {
  printf("\nGMS trace cost for one WEAPON scroll\n");
  printf("%8s %8s %8s %8s %8s\n", "level", "100%", "70%", "30%", "15%");
  std::vector<int> all = levels;
  all.push_back(fifteen_level);
  for (int i = 0; i < static_cast<int>(all.size()); ++i) {
    printf("%8d %8d %8d %8d %8d\n", all[i], WeaponCost(all[i], 100),
           WeaponCost(all[i], 70), WeaponCost(all[i], 30),
           WeaponCost(all[i], 15));
  }
}

// One scroll per slot, which is what a player buys in a sitting. At 70 and 30
// most slots are lost, so the honest comparison is the cost per slot that
// stuck rather than the cost of the pass.
void PrintOnePass(const std::vector<int>& levels, int slots) {
  printf("\none pass over a %d-slot weapon, one scroll a slot (no hammer)\n",
         slots);
  printf("%8s %6s %8s %12s %10s %14s\n", "level", "rate", "traces", "meso",
         "slots hit", "meso per slot");
  for (int i = 0; i < static_cast<int>(levels.size()); ++i) {
    for (int r = 0; r < 3; ++r) {
      double traces = slots * WeaponCost(levels[i], kRates[r]);
      double meso = traces * TraceMeso();
      double landed = slots * kRates[r] / 100.0;
      char meso_text[32];
      char each[32];
      ms::FormatShort(meso, meso_text, sizeof(meso_text));
      ms::FormatShort(landed > 0.0 ? meso / landed : 0.0, each, sizeof(each));
      printf("%8d %5d%% %8.0f %12s %10.1f %14s\n", levels[i], kRates[r], traces,
             meso_text, landed, each);
    }
  }
}

// Filling every slot. Reports the SCROLL spend and how many clean slates the
// job demands, but never what a slate costs -- that price is not GMS's and not
// settled, and this table is what it gets derived from.
void PrintFullFill(const ms::MesoCurve& curve, const std::vector<int>& levels,
                   int slots, double slate_rate) {
  printf(
      "\nfilling all %d slots -- scroll spend only, slates counted not "
      "priced\n",
      slots);
  printf("%8s %6s %10s %12s %10s %12s %9s\n", "level", "rate", "scrolls",
         "scroll meso", "slates", "earned since", "of band");
  int previous = 1;
  for (int i = 0; i < static_cast<int>(levels.size()); ++i) {
    double earned = curve.Earned(previous, levels[i]);
    for (int r = 0; r < 3; ++r) {
      double p = kRates[r] / 100.0;
      double scrolls = slots / p;
      double slates = slots * (1.0 / p - 1.0) / slate_rate;
      double scroll_meso =
          scrolls * WeaponCost(levels[i], kRates[r]) * TraceMeso();
      char a[32];
      char b[32];
      ms::FormatShort(scroll_meso, a, sizeof(a));
      ms::FormatShort(earned, b, sizeof(b));
      printf("%8d %5d%% %10.1f %12s %10.1f %12s %8.1f%%\n", levels[i],
             kRates[r], scrolls, a, slates, b,
             earned > 0.0 ? 100.0 * scroll_meso / earned : 0.0);
    }
    previous = levels[i];
  }
  printf(
      "  what is left of \"earned since\" after the scrolls is the slate "
      "budget:\n  divide it by the slate count for the most a slate could "
      "cost\n");
}

void PrintFifteenRow(const char* label, int slots, int price,
                     double slate_rate) {
  double scrolls = slots / 0.15;
  double slates = slots * (1.0 / 0.15 - 1.0) / slate_rate;
  double scroll_meso = scrolls * price * TraceMeso();
  char a[32];
  ms::FormatShort(scroll_meso, a, sizeof(a));
  printf("%14s %6d %10.1f %14s %10.1f\n", label, slots, scrolls, a, slates);
}

void PrintFifteen(const ms::MesoCurve& curve, const ms::EquipPrototype& equip,
                  double slate_rate) {
  int price = WeaponCost(equip.required_level(), 15);
  printf("\n%s, level %d, at 15%% (%d traces a scroll)\n", equip.name().c_str(),
         equip.required_level(), price);
  printf("%14s %6s %10s %14s %10s\n", "", "slots", "scrolls", "scroll meso",
         "slates");
  PrintFifteenRow("no hammer", equip.upgrade_slots(), price, slate_rate);
  int hammers = absl::GetFlag(FLAGS_hammers);
  if (hammers > 0) {
    PrintFifteenRow("with hammers", equip.upgrade_slots() + hammers, price,
                    slate_rate);
  }
  char text[32];
  ms::FormatShort(curve.Earned(equip.required_level(), 210), text,
                  sizeof(text));
  printf("  earned from level %d to 210: %s meso\n", equip.required_level(),
         text);
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  std::map<std::string, ms::EquipPrototype> equips =
      ms::LoadTextProtoMap<ms::EquipPrototype>(ms::EmbeddedEquips());
  std::map<std::string, ms::Scroll> scrolls =
      ms::LoadTextProtoMap<ms::Scroll>(ms::EmbeddedScrolls());
  std::map<std::string, ms::EquipPrototype>::const_iterator found =
      equips.find(absl::GetFlag(FLAGS_fifteen_equip));
  if (found == equips.end()) {
    LOG(FATAL) << "no equipment named " << absl::GetFlag(FLAGS_fifteen_equip);
  }

  ms::MesoCurveParams params;
  params.etc_stops_at = absl::GetFlag(FLAGS_etc_stops_at);
  ms::MesoCurve curve = ms::BuildMesoCurve(params);

  std::vector<int> levels =
      ms::ParseLevels(absl::GetFlag(FLAGS_weapons), "--weapons");
  int slots = absl::GetFlag(FLAGS_slots);
  double slate_rate = CleanSlateRate(scrolls);

  printf("a spell trace is %.0f meso\n", TraceMeso());
  PrintPriceTable(levels, found->second.required_level());
  PrintOnePass(levels, slots);
  PrintFullFill(curve, levels, slots, slate_rate);
  PrintFifteen(curve, found->second, slate_rate);
  return 0;
}
