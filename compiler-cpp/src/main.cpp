#include <iostream>

#include "cli_options.h"
#include "codegen.h"
#include "frontend.h"
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

  std::vector<kscpp::ir::Spec> specs;
  if (!parse.options.from_ir.empty()) {
    kscpp::ir::Spec spec;
    const auto load_result =
        kscpp::ir::LoadFromFileWithImports(parse.options.from_ir, parse.options.import_paths, &spec);
    if (!load_result.ok) {
      std::cerr << "Error: IR validation failed: " << load_result.error << std::endl;
      return 1;
    }
    specs.push_back(std::move(spec));
  } else {
    kscpp::frontend::ParsedInputs parsed;
    auto parse_stage = kscpp::frontend::ParseKsyInputs(parse.options, &parsed);
    if (!parse_stage.ok) {
      std::cerr << "Error: frontend parse failed: " << parse_stage.error << std::endl;
      return 1;
    }

    auto import_stage = kscpp::frontend::ResolveImports(parse.options, &parsed);
    if (!import_stage.ok) {
      std::cerr << "Error: import resolution failed: " << import_stage.error << std::endl;
      return 1;
    }

    auto lower_stage = kscpp::frontend::LowerToIr(parse.options, parsed, &specs);
    if (!lower_stage.ok) {
      std::cerr << "Error: IR lowering failed: " << lower_stage.error << std::endl;
      return 1;
    }

    auto semantic_stage = kscpp::frontend::ValidateSemanticsAndTypes(specs);
    if (!semantic_stage.ok) {
      std::cerr << "Error: semantic/type validation failed: " << semantic_stage.error << std::endl;
      return 1;
    }
  }

  const bool wants_cpp_stl = parse.options.targets.size() == 1 && parse.options.targets[0] == "cpp_stl";
  const bool wants_cpp17 = parse.options.runtime.cpp_standard == "17";

  if (wants_cpp_stl && wants_cpp17) {
    for (const auto& spec : specs) {
      const auto gen = kscpp::codegen::EmitCppStl17FromIr(spec, parse.options);
      if (!gen.ok) {
        std::cerr << "Error: C++17 IR codegen failed: " << gen.error << std::endl;
        return 1;
      }
      if (!parse.options.from_ir.empty()) {
        std::cout << "IR codegen succeeded: " << spec.name
                  << " (target=cpp_stl, cpp_standard=17)" << std::endl;
      } else {
        std::cout << "Native .ksy codegen succeeded: " << spec.name
                  << " (target=cpp_stl, cpp_standard=17)" << std::endl;
      }
    }
    return 0;
  }

  if (!parse.options.from_ir.empty()) {
    std::cout << "IR validation succeeded: " << specs.front().name << std::endl;
    return 0;
  }

  std::cerr << "Error: native .ksy pipeline currently supports only -t cpp_stl --cpp-standard 17"
            << std::endl;
  return 1;
}
