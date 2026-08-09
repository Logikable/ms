/* What GMS charges in spell traces for one scroll.
 *
 * Source: StrategyWiki, "MapleStory/Spell Trace and Star Force", Upgrade Cost.
 * The live page is behind a Cloudflare challenge, so it was read from the
 * Wayback snapshot of 2026-07-04. Hand-entered rather than generated -- the
 * table has gaps that needed judgement, all of them marked in the .cc.
 *
 * NOTE the shape mismatch: GMS prices per TEN-LEVEL BAND and per equipment
 * category, while data/scrolls prices per ScrollTier, of which there are three.
 * Adopting these numbers in the game means the tier model goes.
 */
#ifndef MS_ANALYSIS_SPELL_TRACE_COST_H_
#define MS_ANALYSIS_SPELL_TRACE_COST_H_

namespace ms {

enum class TraceCategory { kArmor, kGloves, kWeapon, kAccessory };

// Traces one scroll costs for an item of this required level. `success_rate`
// is a whole percent and must be 100, 70, 30 or 15.
//
// Returns 0 where GMS sells no such scroll: 15% is weapon-only below level
// 200, and above 250 only armor is priced.
int SpellTraceCost(int required_level, TraceCategory category,
                   int success_rate);

}  // namespace ms

#endif  // MS_ANALYSIS_SPELL_TRACE_COST_H_
