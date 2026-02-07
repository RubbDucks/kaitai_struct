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

}  // namespace

int main() {
  bool ok = true;

  {
    auto r = Parse({"kscpp", "--help"});
    ok &= Check(r.status == kscpp::ParseStatus::kHelp, "help status");
    ok &= Check(r.message.find("Usage:") != std::string::npos, "help text includes usage");
  }

  {
    auto r = Parse({"kscpp", "--version"});
    ok &= Check(r.status == kscpp::ParseStatus::kVersion, "version status");
    ok &= Check(r.message.find("experimental") != std::string::npos, "version text includes experimental");
  }

  {
    auto r = Parse({"kscpp", "-t", "python", "--read-write", "--debug", "--cpp-standard", "17", "--import-path", "a:b", "in.ksy"});
    ok &= Check(r.status == kscpp::ParseStatus::kOk, "valid parse status");
    ok &= Check(r.options.targets.size() == 1 && r.options.targets[0] == "python", "target parsed");
    ok &= Check(!r.options.runtime.auto_read, "auto_read disabled by debug/read-write");
    ok &= Check(r.options.runtime.read_pos, "debug enables read_pos");
    ok &= Check(!r.options.runtime.zero_copy_substream, "read-write disables zero-copy substream");
    ok &= Check(r.options.runtime.cpp_standard == "17", "cpp-standard parsed");
    ok &= Check(r.options.import_paths.size() == 2, "import-path split");
    ok &= Check(r.options.src_files.size() == 1 && r.options.src_files[0] == "in.ksy", "input file parsed");
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

  return ok ? 0 : 1;
}
