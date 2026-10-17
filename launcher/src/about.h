// The About screen's topics: what this project is, how it works, what it
// needs, credits, the legal note and the build.

#pragma once

#include <string>
#include <vector>

namespace launcher {

struct Topic {
  std::string label;
  std::vector<std::string> paragraphs;
  bool build = false;  // also shows which build this is
};

const std::vector<Topic>& AboutTopics();

// "Build 1a2b3c4, 16 October 2026" (LAUNCHER_COMMIT / LAUNCHER_DATE from CMake).
std::string BuildLine();

}  // namespace launcher
