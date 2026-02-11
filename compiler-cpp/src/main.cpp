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

  const std::string backend_error = kscpp::ValidateBackendCompatibility(parse.options);
  if (!backend_error.empty()) {
    std::cerr << "Error: " << backend_error << std::endl;
    return 1;
  }

  const bool single_target = parse.options.targets.size() == 1;
  const std::string target = single_target ? parse.options.targets[0] : std::string();
  const bool wants_cpp_stl = target == "cpp_stl";
  const bool wants_cpp17 = parse.options.runtime.cpp_standard == "17";

  std::vector<kscpp::ir::Spec> specs;
  if (!parse.options.from_ir.empty()) {
    const auto load_result = kscpp::ir::LoadGraphFromFileWithImports(
        parse.options.from_ir, parse.options.import_paths, &specs);
    if (!load_result.ok) {
      std::cerr << "Error: IR validation failed: " << load_result.error << std::endl;
      return 1;
    }
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

  if (single_target) {
    for (const auto& spec : specs) {
      kscpp::codegen::Result gen;
      std::string target_detail = "target=" + target;
      if (wants_cpp_stl && wants_cpp17) {
        gen = kscpp::codegen::EmitCppStl17FromIr(spec, parse.options);
        target_detail += ", cpp_standard=17";
      } else if (target == "lua") {
        gen = kscpp::codegen::EmitLuaFromIr(spec, parse.options);
      } else if (target == "wireshark_lua") {
        gen = kscpp::codegen::EmitWiresharkLuaFromIr(spec, parse.options);
      } else if (target == "python") {
        gen = kscpp::codegen::EmitPythonFromIr(spec, parse.options);
      } else if (target == "ruby") {
        gen = kscpp::codegen::EmitRubyFromIr(spec, parse.options);
      } else {
        std::cerr << "Error: internal backend dispatch inconsistency after compatibility validation" << std::endl;
        return 1;
      }

      if (!gen.ok) {
        std::cerr << "Error: IR codegen failed: " << gen.error << std::endl;
        return 1;
      }

      if (!parse.options.from_ir.empty()) {
        std::cout << "IR codegen succeeded: " << spec.name << " (" << target_detail << ")" << std::endl;
      } else {
        std::cout << "Native .ksy codegen succeeded: " << spec.name << " (" << target_detail << ")" << std::endl;
      }
    }
    return 0;
  }

  if (!parse.options.from_ir.empty()) {
    std::cout << "IR validation succeeded: " << specs.size() << " module(s)" << std::endl;
    return 0;
  }

  std::cerr << "Error: internal backend dispatch inconsistency after compatibility validation" << std::endl;
  return 1;
}
