#pragma once

#include <string>
#include <vector>

#include "aurora/lang/ast.hpp"

namespace aurora::lang {

struct Diagnostic {
  int line = 1;
  int column = 1;
  std::string message;
};

struct ParseResult {
  bool ok = false;
  AuroraFile file;
  std::vector<Diagnostic> diagnostics;
};

ParseResult ParseAuroraSource(const std::string& source);

}  // namespace aurora::lang

