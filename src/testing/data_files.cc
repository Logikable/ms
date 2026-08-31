#include "src/testing/data_files.h"

#include <memory>
#include <string>

#include "absl/log/check.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {

using bazel::tools::cpp::runfiles::Runfiles;

std::string TestDataDir(const std::string& dir) {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  CHECK(runfiles != nullptr) << err;
  return runfiles->Rlocation("ms/data/" + dir);
}

}  // namespace ms
