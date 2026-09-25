#include "src/audio/track_title.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "src/audio/tracks.h"

namespace ms {
namespace {

// Every track under bgm/, sorted by stem for binary search. A few stems are the
// client's own misspellings (`AcientForest`, `DragonLoad`, `FightingPinkBeen`),
// and the title is the intended name.
struct Title {
  std::string_view track;
  std::string_view title;
};

constexpr Title kTitles[] = {
    {"AboveTheTreetops", "Above the Treetops"},
    {"AbyssCave", "Abyss Cave"},
    {"AcientForest", "Ancient Forest"},
    {"AcientRemain", "Ancient Remain"},
    {"AltarOfAkayrum", "Altar of Akayrum"},
    {"AncientMove", "Ancient Move"},
    {"BattleOnTheDeck", "Battle on the Deck"},
    {"BigMachine", "Big Machine"},
    {"BlackDungeon", "Black Dungeon"},
    {"BlueWorld", "Blue World"},
    {"CaveOfHontale", "Cave of Hontale"},
    {"ChewChew MainTheme", "Chew Chew Main Theme"},
    {"ClockTowerofNightmare", "Clock Tower of Nightmare"},
    {"ConteminatedSea", "Contaminated Sea"},
    {"CygnusGarden", "Cygnus Garden"},
    {"Demian Spine", "Demian Spine"},
    {"Demian True", "Demian True"},
    {"Dispute", "Dispute"},
    {"DragonLoad", "Dragon Road"},
    {"DragonNest", "Dragon Nest"},
    {"ElinForest", "Elin Forest"},
    {"EvilEyes", "Evil Eyes"},
    {"FairyTalediffvers", "Fairy Tale"},
    {"FightingPinkBeen", "Fighting Pink Bean"},
    {"FloralLife", "Floral Life"},
    {"Forgetfulness", "Forgetfulness"},
    {"GuardianSlime-Battle", "Guardian Slime Battle"},
    {"HeartofSuffering", "Heart of Suffering"},
    {"Hieijan_nohime", "Hieijan no Hime"},
    {"HighlandStar", "Highland Star"},
    {"HonTale", "Hon Tale"},
    {"HotDesert", "Hot Desert"},
    {"JoyfulTeaParty", "Joyful Tea Party"},
    {"JungleBook", "Jungle Book"},
    {"JunkYard", "Junk Yard"},
    {"LachelntheIllusionCity", "Lacheln, the Illusion City"},
    {"Lake Of Oblivion", "Lake of Oblivion"},
    {"LowGradeOre", "Low Grade Ore"},
    {"Minar'sDream", "Minar's Dream"},
    {"MoonlightShadow", "Moonlight Shadow"},
    {"MushbudForest", "Mushbud Forest"},
    {"PantheonField", "Pantheon Field"},
    {"QueenPalace", "Queen Palace"},
    {"RedWitch", "Red Witch"},
    {"Remembrance", "Remembrance"},
    {"Repentance", "Repentance"},
    {"RestNPeace", "Rest 'n' Peace"},
    {"Shinin'Harbor", "Shinin' Harbor"},
    {"Start the Adventure", "Start the Adventure"},
    {"Subway", "Subway"},
    {"SunsetDesert", "Sunset Desert"},
    {"Suu1phase", "Suu Phase 1"},
    {"Suu2phase", "Suu Phase 2"},
    {"Suu3phase", "Suu Phase 3"},
    {"TempleInTheMirror", "Temple in the Mirror"},
    {"The Lost City among the Clouds", "The Lost City Among the Clouds"},
    {"TheHolyLand", "The Holy Land"},
    {"TheRaiders", "The Raiders"},
    {"TheTuneOfAzureLight", "The Tune of Azure Light"},
    {"TheWorldsEnd", "The World's End"},
    {"TimeChaos", "Time Chaos"},
    {"Volcanic Zone Of Extinction", "Volcanic Zone of Extinction"},
    {"WarmRegard", "Warm Regard"},
    {"WaveofEmptiness", "Wave of Emptiness"},
    {"Wayout of the wilderness", "Way Out of the Wilderness"},
    {"WelcomeToTheHell", "Welcome to the Hell"},
    {"WhereverYouAre", "Wherever You Are"},
    {"WolfWood", "Wolf Wood"},
    {"destructionPerion", "Destruction Perion"},
    {"destructionTown", "Destruction Town"},
    {"knightsStronghold", "Knights Stronghold"},
    {"thefinalWar", "The Final War"},
};

}  // namespace

std::string TrackTitle(std::string_view track) {
  const Title* it = std::lower_bound(
      std::begin(kTitles), std::end(kTitles), track,
      [](const Title& t, std::string_view name) { return t.track < name; });
  if (it == std::end(kTitles) || it->track != track) {
    return std::string(track);
  }
  return std::string(it->title);
}

std::vector<std::string_view> TracksByTitle() {
  std::vector<std::string_view> tracks = BgmTrackNames();
  std::sort(tracks.begin(), tracks.end(),
            [](std::string_view a, std::string_view b) {
              return TrackTitle(a) < TrackTitle(b);
            });
  return tracks;
}

}  // namespace ms
