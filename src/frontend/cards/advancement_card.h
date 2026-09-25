/* The card shown for a few seconds when the player takes a job advancement.
 *
 * It works like the level-up card: it holds no state, makes no decisions, and
 * the caller decides when it is shown. It is gold for the same reason: an
 * advancement is the biggest event for a character and should be visible from
 * across the room.
 */
#ifndef MS_SRC_FRONTEND_CARDS_ADVANCEMENT_CARD_H_
#define MS_SRC_FRONTEND_CARDS_ADVANCEMENT_CARD_H_

#include "ftxui/dom/elements.hpp"
#include "src/protos/character.pb.h"

namespace ms {

// The card as a bordered window: the old job, a down arrow, and the new job,
// read top to bottom. It is the same size as the level-up card, which appears a
// few seconds before it. `to_stage` names the 5th advancement, which would
// otherwise read "Night Lord" twice.
ftxui::Element AdvancementCard(Job from_job, Job to_job, int to_stage);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CARDS_ADVANCEMENT_CARD_H_
