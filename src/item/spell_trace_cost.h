/* What a scroll costs in spell traces.
 *
 * The price depends on the item, not the scroll: GMS charges by the item's
 * ten-level band and equipment type. A scroll's tier only says what it gives.
 * So the same 30% STR scroll costs 8 traces on a level 70 hat and 20 on a level
 * 100 one.
 *
 * Source: StrategyWiki, "MapleStory/Spell Trace and Star Force", Upgrade Cost.
 * The live page is behind a Cloudflare challenge, so it was read from the
 * Wayback snapshot of 2026-07-04. Entered by hand, not generated, since the
 * table has gaps that needed judgement; each is marked in the .cc.
 */
#ifndef MS_SRC_ITEM_SPELL_TRACE_COST_H_
#define MS_SRC_ITEM_SPELL_TRACE_COST_H_

#include "src/protos/scroll.pb.h"

namespace ms {

// GMS prices these four separately, and hearts are priced from the accessory
// column: they have their own scroll type but not their own price.
enum class TraceCategory { kArmor, kGloves, kWeapon, kAccessory };

// Traces one scroll costs for an item of this required level, with
// `success_rate` as a whole percent. 0 where GMS doesn't sell that scroll: 15%
// is weapon-only below 200, above 250 only armour is priced, and the rates are
// 100, 70, 30 and 15.
int SpellTraceCost(int required_level, TraceCategory category,
                   int success_rate);

// What `scroll` costs on an item of this level; this is what the game calls.
// Where GMS prices the scroll, that price is used and the file's `trace_cost`
// is ignored; where GMS doesn't sell it, the file's price is used. A scroll
// costing nothing would be free, and a data test rejects it.
int TraceCost(const Scroll& scroll, int required_level);

}  // namespace ms

#endif  // MS_SRC_ITEM_SPELL_TRACE_COST_H_
