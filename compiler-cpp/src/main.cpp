#include <iostream>

#include "cli_options.h"
#include "codegen.h"
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
    const auto load_result =
        kscpp::ir::LoadFromFileWithImports(parse.options.from_ir, parse.options.import_paths, &spec);
    if (!load_result.ok) {
      std::cerr << "Error: IR validation failed: " << load_result.error << std::endl;
      return 1;
    }

    const bool wants_cpp_stl = parse.options.targets.size() == 1 && parse.options.targets[0] == "cpp_stl";
    const bool wants_cpp17 = parse.options.runtime.cpp_standard == "17";
    if (wants_cpp_stl && wants_cpp17) {
      const auto gen = kscpp::codegen::EmitCppStl17FromIr(spec, parse.options);
      if (!gen.ok) {
        std::cerr << "Error: C++17 IR codegen failed: " << gen.error << std::endl;
        return 1;
      }
      std::cout << "IR codegen succeeded: " << spec.name << " (target=cpp_stl, cpp_standard=17)"
                << std::endl;
      return 0;
    }

    std::cout << "IR validation succeeded: " << spec.name << std::endl;
    return 0;
  }

  std::cerr << "Not implemented: C++17 compilation path is parser-only in this phase." << std::endl;
  return 1;
}
