#include <filesystem>
#include <iostream>
#include <sstream>

#include "cli_options.h"
#include "codegen.h"
#include "frontend.h"
#include "ir.h"

namespace {
std::string ShellQuote(const std::string& value) {
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') out += "'\\''";
    else out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

int RunDelegatedCompile(const kscpp::CliOptions& options, const std::string& target) {
  const std::filesystem::path compiler_bin =
      std::filesystem::path("compiler") / "jvm" / "target" / "universal" / "stage" / "bin" /
      "kaitai-struct-compiler";
  if (!std::filesystem::exists(compiler_bin)) {
    std::cerr << "Error: Scala stage compiler missing at " << compiler_bin << std::endl;
    return 1;
  }

  std::ostringstream cmd;
  cmd << ShellQuote(compiler_bin.string()) << " -t " << ShellQuote(target) << " -- ";
  if (!options.from_ir.empty()) {
    cmd << "--from-ir " << ShellQuote(options.from_ir) << " ";
  }
  cmd << "-d " << ShellQuote(options.out_dir) << " ";
  for (const auto& import_path : options.import_paths) {
    cmd << "-I " << ShellQuote(import_path) << " ";
  }
  if (!options.runtime.python_package.empty() && target == "python") {
    cmd << "--python-package " << ShellQuote(options.runtime.python_package) << " ";
  }
  if (!options.runtime.php_namespace.empty()) {
    cmd << "--php-namespace " << ShellQuote(options.runtime.php_namespace) << " ";
  }
  if (!options.runtime.java_package.empty()) {
    cmd << "--java-package " << ShellQuote(options.runtime.java_package) << " ";
  }
  if (!options.runtime.go_package.empty()) {
    cmd << "--go-package " << ShellQuote(options.runtime.go_package) << " ";
  }
  if (!options.runtime.nim_module.empty()) {
    cmd << "--nim-module " << ShellQuote(options.runtime.nim_module) << " ";
  }
  if (!options.runtime.nim_opaque.empty()) {
    cmd << "--nim-opaque " << ShellQuote(options.runtime.nim_opaque) << " ";
  }
  if (options.runtime.read_pos) cmd << "--read-pos ";
  if (options.runtime.read_write) cmd << "--read-write ";
  if (!options.runtime.auto_read) cmd << "--no-auto-read ";

  if (options.from_ir.empty()) {
    for (const auto& src : options.src_files) {
      cmd << ShellQuote(src) << " ";
    }
  }

  if (std::system(cmd.str().c_str()) != 0) {
    std::cerr << "Error: delegated backend failed for target=" << target << std::endl;
    return 1;
  }

  std::cout << (options.from_ir.empty() ? "Native .ksy" : "IR")
            << " codegen succeeded: target=" << target << std::endl;
  return 0;
}
} // namespace

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

  const bool single_target = parse.options.targets.size() == 1;
  const std::string target = single_target ? parse.options.targets[0] : std::string();
  const bool wants_cpp_stl = target == "cpp_stl";
  const bool wants_cpp17 = parse.options.runtime.cpp_standard == "17";
  const bool wants_delegated = target == "lua" || target == "wireshark_lua" || target == "python" || target == "ruby";

  if (single_target && wants_delegated) {
    return RunDelegatedCompile(parse.options, target);
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

  if (single_target && wants_cpp_stl && wants_cpp17) {
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

  std::cerr << "Error: native .ksy pipeline currently supports -t cpp_stl --cpp-standard 17 and delegated targets -t lua|wireshark_lua|python|ruby"
            << std::endl;
  return 1;
}
