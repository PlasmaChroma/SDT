#pragma once

#include <string>
#include <vector>

#include "aurora/lang/ast.hpp"

namespace aurora::lang {

struct ValidationResult {
  bool ok = false;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
};

ValidationResult Validate(const AuroraFile& file);

}  // namespace aurora::lang

