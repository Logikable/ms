#include "analysis/meso_curve.h"

#include <algorithm>
#include <vector>

#include "analysis/gms_mob_exp.h"
#include "src/character/exp_table.h"
#include "src/combat/loot.h"
#include "src/protos/mob.pb.h"

namespace ms {

KillValue KillValueAt(int level, const MesoCurveParams& params) {
  Mob mob;
  mob.set_level(level);
  KillValue value;
  value.exp = GmsMobExpPerKill(level);
  // No gear in the curve, so no drop rate either: it measures the economy a
  // character meets rather than one a lucky drop improved.
  value.meso = ExpectedMesoPerKill(mob, 0.0);
  value.etc = params.etc_per_kill * params.etc_price_per_level * level;
  return value;
}

double MesoCurve::Total(int level) const {
  if (level < 0 || level >= static_cast<int>(meso.size())) {
    return 0.0;
  }
  return meso[level] + etc[level];
}

double MesoCurve::Earned(int from, int to) const {
  return std::max(0.0, Total(to) - Total(from));
}

MesoCurve BuildMesoCurve(const MesoCurveParams& params) {
  MesoCurve curve;
  curve.meso.assign(kMaxLevel + 1, 0.0);
  curve.etc.assign(kMaxLevel + 1, 0.0);
  double meso = 0.0;
  double etc = 0.0;
  for (int level = 1; level < kMaxLevel; ++level) {
    KillValue kill = KillValueAt(level, params);
    if (kill.exp > 0.0) {
      double kills = static_cast<double>(ExpToNextLevel(level)) / kill.exp;
      if (level < params.meso_stops_at) {
        meso += kills * kill.meso;
      }
      if (level < params.etc_stops_at) {
        etc += kills * kill.etc;
      }
    }
    curve.meso[level + 1] = meso;
    curve.etc[level + 1] = etc;
  }
  return curve;
}

}  // namespace ms
