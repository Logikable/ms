/* What a scroll costs in spell traces.
 *
 * The price belongs to the ITEM, not to the scroll: GMS charges by the item's
 * ten-level band and by what kind of equipment it is. A scroll's tier says
 * only what the scroll pays out. So the same 30% STR scroll costs 8 traces on
 * a level 70 hat and 20 on a level 100 one.
 *
 * Source: StrategyWiki, "MapleStory/Spell Trace and Star Force", Upgrade Cost.
 * The live page is behind a Cloudflare challenge, so it was read from the
 * Wayback snapshot of 2026-07-04. Hand-entered rather than generated -- the
 * table has gaps that needed judgement, all of them marked in the .cc.
 */
#ifndef MS_SRC_ITEM_SPELL_TRACE_COST_H_
#define MS_SRC_ITEM_SPELL_TRACE_COST_H_

#include "src/protos/scroll.pb.h"

namespace ms {

// GMS prices these four separately. The game has armor and weapons; the other
// two columns are carried because the table was copied whole.
enum class TraceCategory { kArmor, kGloves, kWeapon, kAccessory };

// Traces one scroll costs for an item of this required level. `success_rate`
// is a whole percent and must be 100, 70, 30 or 15.
//
// Returns 0 where GMS sells no such scroll: 15% is weapon-only below level
// 200, and above 250 only armor is priced.
int SpellTraceCost(int required_level, TraceCategory category,
                   int success_rate);

// What `scroll` costs on an item of this level. The one call the game makes.
//
// A clean slate is the exception and carries its own price: GMS does not sell
// one for traces at all, so there is no band to read it from. It is priced by
// the scroll's tier instead, which is the only place a tier still sets a cost.
int TraceCost(const Scroll& scroll, int required_level);

}  // namespace ms

#endif  // MS_SRC_ITEM_SPELL_TRACE_COST_H_
