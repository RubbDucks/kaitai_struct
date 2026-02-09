#include "frontend.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <unistd.h>

namespace kscpp::frontend {
namespace {

std::string ShellQuote(const std::string& value) {
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out.push_back('\'');
  return out;
}

Result Err(std::string message) { return {false, std::move(message)}; }

Result Ok() { return {true, ""}; }

std::filesystem::path DetectScalaCompilerBinary() {
  const std::filesystem::path stage_bin =
      std::filesystem::path("compiler") / "jvm" / "target" / "universal" / "stage" / "bin" /
      "kaitai-struct-compiler";
  if (std::filesystem::exists(stage_bin)) {
    return stage_bin;
  }
  return {};
}

Result ReadFile(const std::filesystem::path& path, std::string* out) {
  std::ifstream in(path);
  if (!in) {
    return Err("unable to open file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  *out = buffer.str();
  return Ok();
}

} // namespace

Result ParseKsyInputs(const CliOptions& options, ParsedInputs* out) {
  out->files.clear();
  if (options.src_files.empty()) {
    return Err("no source .ksy files provided");
  }

  std::unordered_set<std::string> seen;
  for (const auto& src : options.src_files) {
    std::filesystem::path path(src);
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec || !std::filesystem::exists(canonical)) {
      return Err("source file not found: " + src);
    }
    if (canonical.extension() != ".ksy") {
      return Err("source file must have .ksy extension: " + canonical.string());
    }
    const std::string key = canonical.string();
    if (!seen.insert(key).second) {
      continue;
    }
    out->files.push_back({canonical});
  }

  if (out->files.empty()) {
    return Err("no source .ksy files provided");
  }
  return Ok();
}

Result ResolveImports(const CliOptions& options, ParsedInputs* parsed) {
  (void)options;
  for (const auto& input : parsed->files) {
    std::string content;
    auto read = ReadFile(input.source_path, &content);
    if (!read.ok) {
      return read;
    }
    if (content.find("imports:") == std::string::npos) {
      continue;
    }
  }
  return Ok();
}

Result LowerToIr(const CliOptions& options,
                 const ParsedInputs& parsed,
                 std::vector<ir::Spec>* out_specs) {
  out_specs->clear();

  const auto compiler_bin = DetectScalaCompilerBinary();
  if (compiler_bin.empty()) {
    return Err("native .ksy pipeline requires Scala compiler stage binary at "
               "compiler/jvm/target/universal/stage/bin/kaitai-struct-compiler; "
               "run tests/build-compiler first");
  }

  std::error_code ec;
  const auto tmp_root =
      std::filesystem::temp_directory_path(ec) /
      std::filesystem::path("kscpp-native-front-" + std::to_string(::getpid()));
  std::filesystem::create_directories(tmp_root, ec);
  if (ec) {
    return Err("failed to create temporary directory: " + tmp_root.string());
  }

  const std::string lowering_target = options.targets.empty() ? "cpp_stl" : options.targets.front();

  for (size_t i = 0; i < parsed.files.size(); i++) {
    const auto& input = parsed.files[i].source_path;
    const auto ir_path = tmp_root / (input.stem().string() + "-" + std::to_string(i) + ".ksir");

    std::ostringstream cmd;
    cmd << ShellQuote(compiler_bin.string()) << " -t " << ShellQuote(lowering_target)
        << " --emit-ir " << ShellQuote(ir_path.string()) << " -- ";
    for (const auto& import_path : options.import_paths) {
      cmd << "-I " << ShellQuote(import_path) << " ";
    }
    cmd << ShellQuote(input.string());

    const int rc = std::system(cmd.str().c_str());
    if (rc != 0) {
      return Err("failed to lower .ksy to IR for " + input.string() + " (exit=" +
                 std::to_string(rc) + ")");
    }

    ir::Spec spec;
    auto load = ir::LoadFromFileWithImports(ir_path.string(), options.import_paths, &spec);
    if (!load.ok) {
      return Err("failed to load lowered IR for " + input.string() + ": " + load.error);
    }
    out_specs->push_back(std::move(spec));
  }

  return Ok();
}

Result ValidateSemanticsAndTypes(const std::vector<ir::Spec>& specs) {
  for (const auto& spec : specs) {
    const auto result = ir::Validate(spec);
    if (!result.ok) {
      return Err("semantic/type validation failed for " + spec.name + ": " + result.error);
    }
  }
  return Ok();
}

} // namespace kscpp::frontend
