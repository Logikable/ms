#ifndef MS_CHARACTER_H_
#define MS_CHARACTER_H_

#include "src/character.pb.h"

namespace ms {

class CharacterInstance {
 public:
  explicit CharacterInstance(Character character);

  void LevelUp();
  void AdvanceJob(Job next_job);
  // Returns false if `field` is unspecified or `amount` exceeds available AP.
  bool AllocateStat(StatField field, int amount = 1);

  const Character& proto() const { return character_; }

 private:
  Character character_;
};

}  // namespace ms

#endif  // MS_CHARACTER_H_
