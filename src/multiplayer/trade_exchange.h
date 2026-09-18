/* Putting a trade through on this end: whether the bag can take what is
 * coming, and the exchange itself.
 *
 * The server holds no items -- it relays what each side says it is putting up
 * and never asks whether they have it -- so both halves of a trade happen
 * here, against this client's own character and its own catalogs. An item
 * arrives as the state it was saved from and a name; what it IS comes from
 * the reader's catalog, which is what the protocol version covers.
 */
#ifndef MS_SRC_MULTIPLAYER_TRADE_EXCHANGE_H_
#define MS_SRC_MULTIPLAYER_TRADE_EXCHANGE_H_

#include <map>
#include <vector>

#include "src/character/character.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

// Whether the bag can hold `received` once `given` has left it. The two are
// weighed TOGETHER: three equips put up free three slots, so a bag that is
// full right now can still take three back.
//
// False also for an item this build does not have a catalog entry for, which
// is nothing it could put in the bag anyway.
bool HasRoomForTrade(const CharacterInstance& character,
                     const std::map<std::string, ItemPrototype>& items,
                     const TradeOffer& given, const TradeOffer& received);

}  // namespace ms

#endif  // MS_SRC_MULTIPLAYER_TRADE_EXCHANGE_H_
