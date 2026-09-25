/* The shipped data catalogs, loaded from a test's runfiles.
 *
 * A test that needs the real data asks for it here by folder name, and still
 * declares the //data/<folder>:all it reads in its own BUILD rule.
 */
#ifndef MS_SRC_TESTING_DATA_FILES_H_
#define MS_SRC_TESTING_DATA_FILES_H_

#include <map>
#include <string>

#include "src/proto_loader.h"

namespace ms {

// The path of `dir` under data/, found through the runfiles. Dies if the test
// has no runfiles, meaning it isn't being run by Bazel.
std::string TestDataDir(const std::string& dir);

// Every textproto under data/`dir`, keyed by filename stem, read once per test
// binary and returned by reference. Catalogs don't change during a test, and
// parsing them per test used to make the data tests the slowest in the suite.
// `T` must be one of the types proto_loader.cc instantiates LoadTextProtoDir
// for.
template <typename T>
const std::map<std::string, T>& TestData(const std::string& dir) {
  static std::map<std::string, std::map<std::string, T>> by_dir;
  typename std::map<std::string, std::map<std::string, T>>::iterator it =
      by_dir.find(dir);
  if (it == by_dir.end()) {
    it = by_dir.emplace(dir, LoadTextProtoDir<T>(TestDataDir(dir))).first;
  }
  return it->second;
}

// The same catalog as a copy, for a caller that keeps or changes it.
template <typename T>
std::map<std::string, T> LoadTestData(const std::string& dir) {
  return TestData<T>(dir);
}

}  // namespace ms

#endif  // MS_SRC_TESTING_DATA_FILES_H_
