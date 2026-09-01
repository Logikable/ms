#include "analysis/checkpoint.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "absl/log/log.h"
#include "analysis/sim_checkpoint.pb.h"

namespace ms {
namespace {

// The file inside the directory that says which build filled it.
constexpr char kStampFile[] = "stamp";

std::string ReadWholeFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return "";
  }
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

}  // namespace

std::string CheckpointStamp() {
  struct stat info;
  if (stat("/proc/self/exe", &info) != 0) {
    return "";
  }
  char text[128];
  std::snprintf(text, sizeof(text), "%lld.%09ld/%lld",
                static_cast<long long>(info.st_mtim.tv_sec),
                static_cast<long>(info.st_mtim.tv_nsec),
                static_cast<long long>(info.st_size));
  return text;
}

std::string PrepareCheckpointDir(const std::string& sim,
                                 const std::string& stamp) {
  if (stamp.empty()) {
    return "";
  }
  std::error_code failed;
  std::filesystem::path dir =
      std::filesystem::temp_directory_path(failed) / ("ms_" + sim + "_climbs");
  if (failed) {
    return "";
  }
  std::filesystem::create_directories(dir, failed);
  if (failed) {
    return "";
  }
  if (ReadWholeFile(dir / kStampFile) != stamp) {
    // Another build's, and there is no telling what in it moved. Emptied
    // rather than left to be picked over.
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(dir, failed)) {
      std::filesystem::remove_all(entry.path(), failed);
    }
    std::ofstream(dir / kStampFile, std::ios::binary) << stamp;
  }
  return dir.string();
}

bool ReadCheckpoint(const std::string& dir, const std::string& key,
                    const std::string& stamp, SimCheckpoint* out) {
  if (dir.empty()) {
    return false;
  }
  std::string bytes = ReadWholeFile(std::filesystem::path(dir) / (key + ".pb"));
  if (bytes.empty() || !out->ParseFromString(bytes)) {
    return false;
  }
  // Belt as well as braces: the directory is emptied on a stamp that does not
  // match, and a file that somehow outlived that is still refused.
  return out->stamp() == stamp;
}

void WriteCheckpoint(const std::string& dir, const std::string& key,
                     const SimCheckpoint& saved) {
  if (dir.empty()) {
    return;
  }
  std::ofstream out(std::filesystem::path(dir) / (key + ".pb"),
                    std::ios::binary);
  if (!out || !saved.SerializeToOstream(&out)) {
    LOG(WARNING) << "could not write the checkpoint for " << key;
  }
}

}  // namespace ms
