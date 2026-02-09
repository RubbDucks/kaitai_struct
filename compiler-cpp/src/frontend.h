#ifndef KAITAI_STRUCT_COMPILER_CPP_FRONTEND_H_
#define KAITAI_STRUCT_COMPILER_CPP_FRONTEND_H_

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cli_options.h"
#include "ir.h"

namespace kscpp::frontend {

struct Result {
  bool ok = false;
  std::string error;
};

struct ParsedInput {
  std::filesystem::path source_path;
  std::string module_name;
  std::vector<std::string> imports;
};

struct ParsedInputs {
  std::vector<ParsedInput> files;
  std::unordered_map<std::string, ir::Spec> lowered_by_path;
};

Result ParseKsyInputs(const CliOptions& options, ParsedInputs* out);
Result ResolveImports(const CliOptions& options, ParsedInputs* parsed);
Result LowerToIr(const CliOptions& options,
                 const ParsedInputs& parsed,
                 std::vector<ir::Spec>* out_specs);
Result ValidateSemanticsAndTypes(const std::vector<ir::Spec>& specs);

} // namespace kscpp::frontend

#endif
