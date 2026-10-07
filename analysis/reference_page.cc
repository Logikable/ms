/* reference_page: writes the player reference page for cubes, star force and
 * Inner Ability, with every table read from the game.
 *
 *   bazelisk run //analysis:reference_page -- --out=/tmp/reference_page.html
 *
 * The page is analysis/reference_page.html with the data put in; publish the
 * output, never the template.
 */
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "analysis/reference_data.h"
#include "src/embedded_data.h"
#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"

ABSL_FLAG(std::string, out, "reference_page.html",
          "Where to write the page. Relative to the directory bazel run was "
          "called from.");
ABSL_FLAG(std::string, page_template, "analysis/reference_page.html",
          "The page to put the data in, relative to the runfiles.");

namespace ms {
namespace {

std::string ReadFile(const std::string& path) {
  std::ifstream in(path);
  CHECK(in) << "Can't read " << path;
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// bazel run starts in the runfiles; a relative --out means the caller's
// directory.
std::string OutPath(const std::string& out) {
  const char* caller = std::getenv("BUILD_WORKING_DIRECTORY");
  if (out.empty() || out[0] == '/' || caller == nullptr) {
    return out;
  }
  return std::string(caller) + "/" + out;
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  const std::string json = ms::ReferenceDataJson(
      ms::LoadTextProtoMap<ms::EquipPrototype>(ms::EmbeddedEquips()));
  const std::string page =
      ms::ReferencePage(ms::ReadFile(absl::GetFlag(FLAGS_page_template)), json);
  const std::string path = ms::OutPath(absl::GetFlag(FLAGS_out));
  std::ofstream out(path);
  CHECK(out) << "Can't write " << path;
  out << page;
  LOG(INFO) << "Wrote " << path << " (" << page.size() << " bytes)";
  return 0;
}
