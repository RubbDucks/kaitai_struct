#include <iostream>
#include <string>
#include <vector>

#include "cli_options.h"

namespace {

bool Check(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    return false;
  }
  return true;
}

kscpp::ParseResult Parse(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const std::string& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  return kscpp::ParseCommandLine(static_cast<int>(argv.size()), argv.data());
}

bool CheckBackendError(const std::vector<std::string>& args, const std::string& expected_substr,
                       const std::string& message) {
  const auto r = Parse(args);
  if (r.status != kscpp::ParseStatus::kOk) {
    return Check(false, message + " (parse failed unexpectedly)");
  }
  const std::string backend_error = kscpp::ValidateBackendCompatibility(r.options);
  return Check(backend_error.find(expected_substr) != std::string::npos,
               message + " (actual: " + backend_error + ")");
}

} // namespace

int main() {
  bool ok = true;

  {
    auto r = Parse({"kscpp", "--help"});
    ok &= Check(r.status == kscpp::ParseStatus::kHelp, "help status");
    ok &= Check(r.message.find("Usage:") != std::string::npos, "help text includes usage");
    ok &= Check(r.message.find("--from-ir") != std::string::npos, "help text includes from-ir");
  }

  {
    auto r = Parse({"kscpp", "--version"});
    ok &= Check(r.status == kscpp::ParseStatus::kVersion, "version status");
    ok &= Check(r.message.find("experimental") != std::string::npos,
                "version text includes experimental");
  }

  {
    auto r = Parse({"kscpp", "-t", "python", "--read-write", "--debug", "--import-path", "a:b",
                    "in.ksy"});
    ok &= Check(r.status == kscpp::ParseStatus::kOk, "valid parse status");
    ok &= Check(r.options.targets.size() == 1 && r.options.targets[0] == "python", "target parsed");
    ok &= Check(!r.options.runtime.auto_read, "auto_read disabled by debug/read-write");
    ok &= Check(r.options.runtime.read_pos, "debug enables read_pos");
    ok &= Check(!r.options.runtime.zero_copy_substream, "read-write disables zero-copy substream");
    ok &= Check(r.options.import_paths.size() == 2, "import-path split");
    ok &= Check(r.options.src_files.size() == 1 && r.options.src_files[0] == "in.ksy",
                "input file parsed");
    ok &= Check(kscpp::ValidateBackendCompatibility(r.options).empty(),
                "python read-write/debug combination accepted by backend");
  }

  {
    auto r = Parse({"kscpp", "--from-ir", "sample.ksir", "-t", "cpp_stl", "--cpp-standard", "17"});
    ok &= Check(r.status == kscpp::ParseStatus::kOk, "from-ir parse status with target");
    ok &= Check(kscpp::ValidateBackendCompatibility(r.options).empty(),
                "from-ir cpp_stl 17 accepted by backend");
  }

  {
    auto r = Parse({"kscpp", "--from-ir", "sample.ksir", "input.ksy"});
    ok &= Check(r.status == kscpp::ParseStatus::kError, "from-ir rejects source files");
  }

  {
    auto r = Parse({"kscpp", "-t", "python", "--cpp-standard", "20"});
    ok &= Check(r.status == kscpp::ParseStatus::kError, "invalid cpp standard rejected");
  }

  {
    auto r = Parse({"kscpp", "-t", "invalid_lang"});
    ok &= Check(r.status == kscpp::ParseStatus::kError, "invalid target rejected");
  }

  {
    auto r = Parse({"kscpp", "--target", "python", "--unknown"});
    ok &= Check(r.status == kscpp::ParseStatus::kError, "unknown option rejected");
  }

  {
    auto r = Parse({"kscpp", "in.ksy"});
    ok &= Check(r.status == kscpp::ParseStatus::kError, "missing target rejected");
  }

  ok &= CheckBackendError({"kscpp", "-t", "all", "in.ksy"},
                          "not implemented in compiler-cpp backend",
                          "accepted CLI target 'all' is fail-fast rejected by backend");

  ok &= CheckBackendError({"kscpp", "-t", "cpp_stl", "--cpp-standard", "98", "in.ksy"},
                          "requires --cpp-standard 17",
                          "cpp_stl requires cpp17 in backend");

  ok &= CheckBackendError({"kscpp", "-t", "ruby", "--read-write", "in.ksy"},
                          "--read-write is not supported for target 'ruby'",
                          "--read-write target interaction rejected for ruby");

  ok &= CheckBackendError({"kscpp", "-t", "python", "--no-auto-read", "in.ksy"},
                          "--no-auto-read currently requires --read-write or --read-pos",
                          "--no-auto-read alone rejected consistently");

  ok &= CheckBackendError({"kscpp", "-t", "lua", "--python-package", "pkg", "in.ksy"},
                          "--python-package is only supported with target 'python'",
                          "python-package rejected for non-python delegated target");

  {
    auto r = Parse({"kscpp", "-t", "python", "--no-auto-read", "--read-pos", "in.ksy"});
    ok &= Check(r.status == kscpp::ParseStatus::kOk, "python no-auto-read+read-pos parse status");
    ok &= Check(kscpp::ValidateBackendCompatibility(r.options).empty(),
                "--no-auto-read + --read-pos accepted");
  }

  {
    auto r = Parse({"kscpp", "-t", "python", "-t", "ruby", "in.ksy"});
    ok &= Check(r.status == kscpp::ParseStatus::kOk, "multi-target parse status");
    const std::string backend_error = kscpp::ValidateBackendCompatibility(r.options);
    ok &= Check(backend_error.find("multiple targets are not supported") != std::string::npos,
                "multi-target rejected by backend compatibility validator");
  }

  return ok ? 0 : 1;
}
