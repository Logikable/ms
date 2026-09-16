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

// GMS prices these four separately, and a heart bills from the accessory
// column: its shelf is its own, its price is not.
enum class TraceCategory { kArmor, kGloves, kWeapon, kAccessory };

// Traces one scroll costs for an item of this required level, `success_rate` a
// whole percent. 0 where GMS sells no such scroll: 15% is weapon-only below
// 200, above 250 only armour is priced, and the rates are 100, 70, 30, 15.
int SpellTraceCost(int required_level, TraceCategory category,
                   int success_rate);

// What `scroll` costs on an item of this level -- THE call the game makes.
// Where GMS prices the scroll that price wins and the file's `trace_cost` is
// ignored; where it sells no such scroll, the file's price stands. A scroll
// costing nothing is one given away, and a data test refuses it.
int TraceCost(const Scroll& scroll, int required_level);

}  // namespace ms

#endif  // MS_SRC_ITEM_SPELL_TRACE_COST_H_
