#include <iostream>

#include "cli_options.h"
#include "ir.h"

int main(int argc, char** argv) {
  const kscpp::ParseResult parse = kscpp::ParseCommandLine(argc, argv);

  if (parse.status == kscpp::ParseStatus::kHelp || parse.status == kscpp::ParseStatus::kVersion) {
    std::cout << parse.message << std::endl;
    return 0;
  }

  if (parse.status == kscpp::ParseStatus::kError) {
    std::cerr << "Error: " << parse.message << std::endl;
    std::cerr << "Try '--help' for usage." << std::endl;
    return 1;
  }

  if (!parse.options.from_ir.empty()) {
    kscpp::ir::Spec spec;
    const auto load_result = kscpp::ir::LoadFromFile(parse.options.from_ir, &spec);
    if (!load_result.ok) {
      std::cerr << "Error: IR validation failed: " << load_result.error << std::endl;
      return 1;
    }
    std::cout << "IR validation succeeded: " << spec.name << std::endl;
    return 0;
  }

  std::cerr << "Not implemented: C++17 compilation path is parser-only in this phase." << std::endl;
  return 1;
}
