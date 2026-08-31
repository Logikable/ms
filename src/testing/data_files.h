/* The shipped catalogs, loaded from a test's runfiles.
 *
 * Eight test files were each writing out their own Runfiles::CreateForTest and
 * their own one-line wrapper per folder -- nineteen of them between the lot.
 * A test that wants the real data asks for it by folder name here, and still
 * declares the //data/<folder>:all it reads in its own BUILD rule.
 */
#ifndef MS_SRC_TESTING_DATA_FILES_H_
#define MS_SRC_TESTING_DATA_FILES_H_

#include <map>
#include <string>

#include "src/proto_loader.h"

namespace ms {

// The path of the shipped `dir` under data/, found through the runfiles. Dies
// if the test has none, which means it is not being run by Bazel.
std::string TestDataDir(const std::string& dir);

// Every textproto under data/`dir`, keyed by filename stem. `T` must be one of
// the types proto_loader.cc instantiates LoadTextProtoDir for.
template <typename T>
std::map<std::string, T> LoadTestData(const std::string& dir) {
  return LoadTextProtoDir<T>(TestDataDir(dir));
}

}  // namespace ms

#endif  // MS_SRC_TESTING_DATA_FILES_H_
