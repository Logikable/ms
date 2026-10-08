#include "src/frontend/widgets/familiar_list.h"

#include <gtest/gtest.h>

#include <string>

#include "src/character/familiar.h"
#include "src/frontend/widgets/text_columns.h"

namespace ms {
namespace {

FamiliarBook OneLevelled() {
  FamiliarBook book;
  Familiar* snail = book.add_familiars();
  snail->set_name("Snail");
  snail->set_nickname("Gary");
  snail->set_level(4);
  FamiliarLine* boss = snail->add_lines();
  boss->set_type(FAMILIAR_LINE_TYPE_BOSS_DAMAGE_40);
  boss->set_rank(POTENTIAL_RANK_LEGENDARY);
  FamiliarLine* str = snail->add_lines();
  str->set_type(FAMILIAR_LINE_TYPE_STR);
  str->set_rank(POTENTIAL_RANK_EPIC);
  return book;
}

// Every row is as wide as the header over it, whatever it holds, so the
// columns line up.
TEST(FamiliarListTest, RowsAreAsWideAsTheHeader) {
  for (bool in_use : {false, true}) {
    const FamiliarColumns columns = FitFamiliarColumns(83, in_use);
    const int header = TextColumns(FamiliarHeader(columns));
    EXPECT_EQ(header, 83);
    for (const FamiliarSpecies& species : FamiliarRoster()) {
      const FamiliarCells cells =
          FamiliarRowCells(OneLevelled(), species.name, columns, in_use);
      const std::string row = "  " + cells.name + "  " + cells.mob + "  " +
                              cells.level + "  " + cells.potential +
                              (in_use ? "  " + cells.in_use : "") + " ";
      EXPECT_EQ(TextColumns(row), header) << species.name;
    }
  }
}

// A renamed familiar shows its own name beside the mob it is, and its lines
// value first, as an item's potential column does. One never levelled reads
// "-".
TEST(FamiliarListTest, ARowNamesTheFamiliarAndItsLines) {
  const FamiliarColumns columns = FitFamiliarColumns(83, true);
  const FamiliarCells snail =
      FamiliarRowCells(OneLevelled(), "Snail", columns, true);
  EXPECT_EQ(snail.name.substr(0, 4), "Gary");
  EXPECT_EQ(snail.mob.substr(0, 5), "Snail");
  EXPECT_EQ(snail.level.substr(0, 1), "4");
  EXPECT_EQ(snail.potential.substr(0, 17), "40% Boss, 12 STR ");
  EXPECT_NE(snail.in_use.find("✓"), std::string::npos);
  EXPECT_EQ(snail.rank, POTENTIAL_RANK_LEGENDARY);

  const FamiliarCells slime =
      FamiliarRowCells(OneLevelled(), "Slime", columns, false);
  EXPECT_EQ(slime.name.substr(0, 6), "Slime ");
  EXPECT_EQ(slime.potential.substr(0, 2), "- ");
  EXPECT_EQ(slime.in_use.find("✓"), std::string::npos);
  EXPECT_EQ(slime.rank, POTENTIAL_RANK_UNSPECIFIED);
}

}  // namespace
}  // namespace ms
