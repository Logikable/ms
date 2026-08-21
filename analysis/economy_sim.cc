/* What the game's economy asks of a player: what they earn, and what the two
 * upgrade systems charge for it.
 *
 * Three sections, each on its own flag.
 *
 *   --curve   what a player has earned on reaching a level, 1 to 300. A climb
 *             through the real engine stops at the level cap, because that is
 *             where the maps stop; this is the closed form that carries it the
 *             rest of the way (analysis/meso_curve.h). The block of played
 *             levels is the check: it should sit within a few percent of the
 *             meso //analysis:level_sim earns on the way up.
 *   --traces  what scrolling a weapon costs. The trace prices are GMS's
 *             (src/item/spell_trace_cost.h); what this adds is how many
 *             scrolls a job actually takes and whether the player has the
 *             meso for it by the level they want it done.
 *   --stars   what star forcing that same weapon costs, failures and booms
 *             priced in (analysis/star_force_curve.h).
 *
 * The three belong together because only the first one says whether the other
 * two are affordable, and both of them are priced against it here.
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
#include "analysis/star_force_curve.h"
#include "src/character/exp_table.h"
#include "src/embedded_data.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/spell_trace_cost.h"
#include "src/item/star_force_cost.h"
#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

ABSL_FLAG(bool, curve, true, "Print what a player earns.");
ABSL_FLAG(bool, traces, true, "Print what spell trace scrolling costs.");
ABSL_FLAG(bool, stars, true, "Print what star forcing costs.");
ABSL_FLAG(std::string, milestones, "40,110,200,230,260",
          "Levels the earnings table reports at.");
ABSL_FLAG(int, meso_stops_at, 100000,
          "First level at which mobs stop dropping meso.");
ABSL_FLAG(double, trace_meso, 5000.0, "Meso one spell trace costs.");
ABSL_FLAG(std::string, weapons, "40,70,100,140",
          "Weapon levels to scroll and to star force.");
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
ABSL_FLAG(int, etc_stops_at, 200,
          "First level at which mobs stop dropping Etc items. GMS's Arcane "
          "River drops nothing from 200. Past the table to disable.");

namespace {

const int kRates[] = {100, 70, 30};

// --- what a player earns ---------------------------------------------------

void PrintMilestones(const ms::MesoCurve& curve) {
  printf("\ncumulative meso earned, nothing spent");
  if (absl::GetFlag(FLAGS_etc_stops_at) <= ms::kMaxLevel) {
    printf(" (Etc drops stop at %d)", absl::GetFlag(FLAGS_etc_stops_at));
  }
  printf("\n%6s %18s %18s %10s\n", "level", "meso only", "+ Etc sold",
         "Etc share");
  std::vector<int> levels =
      ms::ParseLevels(absl::GetFlag(FLAGS_milestones), "--milestones");
  for (int i = 0; i < static_cast<int>(levels.size()); ++i) {
    int level = levels[i];
    double both = curve.Total(level);
    printf("%6d %18.0f %18.0f %9.1f%%\n", level, curve.meso[level], both,
           both > 0.0 ? 100.0 * curve.etc[level] / both : 0.0);
  }
}

// The only levels the game can be played across, so the only place the model
// can be held against something measured. These are level_sim's own
// milestones, so the two tables line up row for row.
void PrintEngineCheck(const ms::MesoCurve& curve) {
  printf("\nthe range //analysis:level_sim can check by playing\n");
  for (int level = 10; level <= 140; level += 10) {
    printf("%6d %18.0f\n", level, curve.Total(level));
  }
}

// Why the curve flattens: meso per kill is linear in mob level and capped by
// MeanMesoMultiplier above 90, while mob EXP keeps climbing.
void PrintPerKill(const ms::MesoCurveParams& params) {
  printf("\nwhat one kill pays, by level\n");
  printf("%6s %12s %12s %12s %12s\n", "level", "EXP", "meso", "Etc",
         "meso per EXP");
  const int kRows[] = {10, 30, 60, 90, 110, 140, 170, 199, 230, 260};
  for (int i = 0; i < 10; ++i) {
    ms::KillValue kill = ms::KillValueAt(kRows[i], params);
    char exp[32];
    ms::FormatShort(kill.exp, exp, sizeof(exp));
    printf("%6d %12s %12.0f %12.0f %12.3f\n", kRows[i], exp, kill.meso,
           kill.etc, kill.exp > 0.0 ? (kill.meso + kill.etc) / kill.exp : 0.0);
  }
}

// --- what spell traces cost ------------------------------------------------

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

// --- what star force costs -------------------------------------------------

// The star counts worth a row. Every shelf and wall in the rate table: 15 is
// where booms begin, 17 and 18 are the walls, 20 and 22 the shelves after
// them.
const int kStarTargets[] = {5, 8, 10, 12, 15, 17, 20, 22, 25, 30};
constexpr int kNumStarTargets = sizeof(kStarTargets) / sizeof(kStarTargets[0]);

// What one attempt costs at each star, for every level in the table. A blank
// cell is a star that level of item cannot reach.
void PrintStarPrices(const std::vector<int>& levels) {
  printf("\nGMS meso for ONE star force attempt, paid win or lose\n");
  printf("%8s", "star");
  for (int i = 0; i < static_cast<int>(levels.size()); ++i) {
    printf(" %12s", ("Lv" + std::to_string(levels[i])).c_str());
  }
  printf("\n");
  for (int stars = 0; stars < ms::kMaxStarForce; ++stars) {
    char step[16];
    snprintf(step, sizeof(step), "%d>%d", stars, stars + 1);
    printf("%8s", step);
    for (int i = 0; i < static_cast<int>(levels.size()); ++i) {
      if (stars >= ms::EquipTabItem::MaxStarsForLevel(levels[i])) {
        printf(" %12s", "-");
        continue;
      }
      char text[32];
      ms::FormatShort(ms::StarForceCost(levels[i], stars), text, sizeof(text));
      printf(" %12s", text);
    }
    printf("\n");
  }
}

// One item's whole run, from bare to each star it can hold. `earned` is what
// the player takes in over the band this item is worn for, so the last column
// is the share of a level band the stars would eat.
void PrintStarRuns(int level, double earned) {
  int cap = ms::EquipTabItem::MaxStarsForLevel(level);
  printf("\nlevel %d, 0 stars to each target it can hold (cap %d)\n", level,
         cap);
  printf("%8s %10s %8s %14s %14s %9s\n", "target", "attempts", "booms", "meso",
         "earned since", "of band");
  for (int i = 0; i < kNumStarTargets; ++i) {
    if (kStarTargets[i] > cap) {
      break;
    }
    ms::StarForceRun run = ms::StarForceRunTo(level, 0, kStarTargets[i]);
    char meso[32];
    char band[32];
    ms::FormatShort(run.meso, meso, sizeof(meso));
    ms::FormatShort(earned, band, sizeof(band));
    // Past a few times the band the exact multiple says nothing the meso
    // column has not: the target is out of reach and that is the answer.
    double share = earned > 0.0 ? 100.0 * run.meso / earned : 0.0;
    char of_band[16];
    if (share > 9999.0) {
      snprintf(of_band, sizeof(of_band), ">9999%%");
    } else {
      snprintf(of_band, sizeof(of_band), "%.1f%%", share);
    }
    printf("%7d\u2605 %10.1f %8.2f %14s %14s %9s\n", kStarTargets[i],
           run.attempts, run.booms, meso, band, of_band);
  }
}

void PrintStars(const ms::MesoCurve& curve, const std::vector<int>& levels,
                const ms::EquipPrototype& fifteen) {
  PrintStarPrices(levels);
  std::vector<int> all = levels;
  if (fifteen.required_level() > 0) {
    all.push_back(fifteen.required_level());
  }
  int previous = 1;
  for (int i = 0; i < static_cast<int>(all.size()); ++i) {
    PrintStarRuns(all[i], curve.Earned(previous, all[i]));
    previous = all[i];
  }
  printf(
      "\n  A boom is one more copy of the item, which this counts and does "
      "not price:\n  what a copy costs depends on where it came from, and a "
      "drop-only item has no price.\n  The trace hands the item back at 12 to "
      "20 stars, and the run carries on from there.\n  Nothing protects an "
      "attempt -- no star catch, no safeguard -- which is the game as it\n  "
      "ships, and is why the last few stars read as they do.\n");
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
  params.meso_stops_at = absl::GetFlag(FLAGS_meso_stops_at);
  ms::MesoCurve curve = ms::BuildMesoCurve(params);

  std::vector<int> levels =
      ms::ParseLevels(absl::GetFlag(FLAGS_weapons), "--weapons");
  int slots = absl::GetFlag(FLAGS_slots);
  double slate_rate = CleanSlateRate(scrolls);

  if (absl::GetFlag(FLAGS_curve)) {
    PrintMilestones(curve);
    PrintEngineCheck(curve);
    PrintPerKill(params);
  }
  if (absl::GetFlag(FLAGS_traces)) {
    printf("\na spell trace is %.0f meso\n", TraceMeso());
    PrintPriceTable(levels, found->second.required_level());
    PrintOnePass(levels, slots);
    PrintFullFill(curve, levels, slots, slate_rate);
    PrintFifteen(curve, found->second, slate_rate);
  }
  if (absl::GetFlag(FLAGS_stars)) {
    PrintStars(curve, levels, found->second);
  }
  printf("\n");
  return 0;
}
