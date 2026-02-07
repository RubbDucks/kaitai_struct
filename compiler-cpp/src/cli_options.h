#ifndef KAITAI_STRUCT_COMPILER_CPP_CLI_OPTIONS_H_
#define KAITAI_STRUCT_COMPILER_CPP_CLI_OPTIONS_H_

#include <string>
#include <vector>

namespace kscpp {

struct RuntimeOptions {
  bool read_write = false;
  bool auto_read = true;
  bool read_pos = false;
  bool zero_copy_substream = true;
  bool opaque_types = false;

  std::string cpp_namespace;
  std::string cpp_standard = "98";
  std::string go_package;
  std::string java_package;
  std::string java_from_file_class;
  std::string dotnet_namespace;
  std::string php_namespace;
  std::string python_package;
  std::string nim_module;
  std::string nim_opaque;
};

struct CliOptions {
  std::vector<std::string> verbose;
  std::vector<std::string> src_files;
  std::string out_dir = ".";
  std::vector<std::string> targets;
  bool throw_exceptions = false;
  bool json_output = false;
  std::vector<std::string> import_paths;
  RuntimeOptions runtime;
};

enum class ParseStatus {
  kOk,
  kHelp,
  kVersion,
  kError,
};

struct ParseResult {
  ParseStatus status = ParseStatus::kError;
  CliOptions options;
  std::string message;
};

ParseResult ParseCommandLine(int argc, char** argv);
std::string HelpText();
std::string VersionText();

}  // namespace kscpp

#endif
