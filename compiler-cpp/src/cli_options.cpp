#include "cli_options.h"

#include <algorithm>
#include <sstream>
#include <string_view>

namespace kscpp {
namespace {

constexpr std::string_view kVersion = "0.0.0";
constexpr std::string_view kName = "kaitai-struct-compiler";

const std::vector<std::string>& ValidTargets() {
  static const std::vector<std::string> targets = {
      "all", "cpp_stl", "csharp", "go",  "graphviz", "html", "java", "javascript",
      "lua", "nim",     "perl",   "php", "python",   "ruby", "rust", "wireshark_lua"};
  return targets;
}

bool IsValidTarget(const std::string& target) {
  const auto& valid = ValidTargets();
  return std::find(valid.begin(), valid.end(), target) != valid.end();
}

bool IsValidCppStandard(const std::string& standard) {
  return standard == "98" || standard == "11" || standard == "17";
}

bool IsKnownOption(const std::string& arg) {
  static const std::vector<std::string> options = {"-t",
                                                   "--target",
                                                   "-w",
                                                   "--read-write",
                                                   "-d",
                                                   "--outdir",
                                                   "-I",
                                                   "--import-path",
                                                   "--cpp-namespace",
                                                   "--cpp-standard",
                                                   "--go-package",
                                                   "--java-package",
                                                   "--java-from-file-class",
                                                   "--dotnet-namespace",
                                                   "--php-namespace",
                                                   "--python-package",
                                                   "--nim-module",
                                                   "--nim-opaque",
                                                   "--opaque-types",
                                                   "--zero-copy-substream",
                                                   "--ksc-exceptions",
                                                   "--ksc-json-output",
                                                   "--verbose",
                                                   "--no-auto-read",
                                                   "--read-pos",
                                                   "--debug",
                                                   "--from-ir",
                                                   "--help",
                                                   "-h",
                                                   "--version"};
  return std::find(options.begin(), options.end(), arg) != options.end();
}

bool IsBoolValue(const std::string& value) { return value == "true" || value == "false"; }

const std::vector<std::string>& SupportedTargets() {
  static const std::vector<std::string> supported = {"cpp_stl", "lua", "python", "ruby", "wireshark_lua"};
  return supported;
}

bool SupportsReadWriteTarget(const std::string& target) {
  return target == "cpp_stl" || target == "python";
}

std::string Join(const std::vector<std::string>& values, const std::string& sep) {
  std::ostringstream out;
  for (size_t i = 0; i < values.size(); i++) {
    if (i > 0) {
      out << sep;
    }
    out << values[i];
  }
  return out.str();
}

} // namespace

std::string HelpText() {
  std::ostringstream out;
  out << kName << " " << kVersion << "\n"
      << "Usage: kaitai-struct-compiler [options] <file>...\n\n"
      << "Options:\n"
      << "  -t, --target <language>           target languages (all, cpp_stl, csharp, go, "
         "graphviz, html, java, javascript, lua, nim, perl, php, python, ruby, rust, "
         "wireshark_lua)\n"
      << "  -w, --read-write                  generate read-write support in classes\n"
      << "  -d, --outdir <directory>          output directory\n"
      << "  -I, --import-path <paths>         .ksy import paths (colon/semicolon-separated)\n"
      << "      --cpp-namespace <namespace>   C++ namespace\n"
      << "      --cpp-standard <standard>     C++ standard to target (98, 11, 17)\n"
      << "      --go-package <package>        Go package\n"
      << "      --java-package <package>      Java package\n"
      << "      --java-from-file-class <class> Java fromFile() helper class\n"
      << "      --dotnet-namespace <ns>       .NET namespace\n"
      << "      --php-namespace <ns>          PHP namespace\n"
      << "      --python-package <package>    Python package\n"
      << "      --nim-module <module>         Nim runtime module\n"
      << "      --nim-opaque <module>         Nim opaque module directory\n"
      << "      --opaque-types <bool>         opaque types allowed\n"
      << "      --zero-copy-substream <bool>  zero-copy substreams allowed\n"
      << "      --ksc-exceptions              throw exceptions instead of readable errors\n"
      << "      --ksc-json-output             output compilation results as JSON\n"
      << "      --verbose <subsystem>         verbose output\n"
      << "      --no-auto-read                disable auto-running _read in constructor\n"
      << "      --read-pos                    _read remembers attribute positions in stream\n"
      << "      --debug                       same as --no-auto-read --read-pos\n"
      << "      --from-ir <path>              load and validate migration IR sidecar\n"
      << "  -h, --help                        display this help and exit\n"
      << "      --version                     output version information and exit\n";
  return out.str();
}

std::string VersionText() {
  std::ostringstream out;
  out << "kscpp " << kVersion << " experimental";
  return out.str();
}

ParseResult ParseCommandLine(int argc, char** argv) {
  ParseResult result;
  result.status = ParseStatus::kOk;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];

    if (arg == "-h" || arg == "--help") {
      result.status = ParseStatus::kHelp;
      result.message = HelpText();
      return result;
    }
    if (arg == "--version") {
      result.status = ParseStatus::kVersion;
      result.message = VersionText();
      return result;
    }

    auto require_value = [&](const std::string& flag) -> const char* {
      if (i + 1 >= argc) {
        result.status = ParseStatus::kError;
        result.message = "option requires an argument: " + flag;
        return nullptr;
      }
      i++;
      return argv[i];
    };

    if (arg == "-t" || arg == "--target") {
      const char* value = require_value(arg);
      if (!value) {
        return result;
      }
      std::string target(value);
      if (!IsValidTarget(target)) {
        result.status = ParseStatus::kError;
        result.message = "'" + target + "' is not a valid target language; valid ones are: " +
                         Join(ValidTargets(), ", ");
        return result;
      }
      result.options.targets.push_back(target);
      continue;
    }

    if (arg == "-w" || arg == "--read-write") {
      result.options.runtime.read_write = true;
      result.options.runtime.auto_read = false;
      continue;
    }

    if (arg == "-d" || arg == "--outdir") {
      const char* value = require_value(arg);
      if (!value) {
        return result;
      }
      result.options.out_dir = value;
      continue;
    }

    if (arg == "-I" || arg == "--import-path") {
      const char* value = require_value(arg);
      if (!value) {
        return result;
      }
      std::string paths(value);
      std::string current;
      for (char c : paths) {
#ifdef _WIN32
        if (c == ';') {
#else
        if (c == ':') {
#endif
          if (!current.empty()) {
            result.options.import_paths.push_back(current);
            current.clear();
          }
        } else {
          current.push_back(c);
        }
      }
      if (!current.empty()) {
        result.options.import_paths.push_back(current);
      }
      continue;
    }

    if (arg == "--cpp-namespace") {
      const char* value = require_value(arg);
      if (!value) {
        return result;
      }
      result.options.runtime.cpp_namespace = value;
      continue;
    }

    if (arg == "--cpp-standard") {
      const char* value = require_value(arg);
      if (!value) {
        return result;
      }
      std::string standard(value);
      if (!IsValidCppStandard(standard)) {
        result.status = ParseStatus::kError;
        result.message =
            "'" + standard + "' is not a valid C++ standard to target; valid ones are: 98, 11, 17";
        return result;
      }
      result.options.runtime.cpp_standard = standard;
      continue;
    }

    if (arg == "--go-package") {
      const char* value = require_value(arg);
      if (!value)
        return result;
      result.options.runtime.go_package = value;
      continue;
    }
    if (arg == "--java-package") {
      const char* value = require_value(arg);
      if (!value)
        return result;
      result.options.runtime.java_package = value;
      continue;
    }
    if (arg == "--java-from-file-class") {
      const char* value = require_value(arg);
      if (!value)
        return result;
      result.options.runtime.java_from_file_class = value;
      continue;
    }
    if (arg == "--dotnet-namespace") {
      const char* value = require_value(arg);
      if (!value)
        return result;
      result.options.runtime.dotnet_namespace = value;
      continue;
    }
    if (arg == "--php-namespace") {
      const char* value = require_value(arg);
      if (!value)
        return result;
      result.options.runtime.php_namespace = value;
      continue;
    }
    if (arg == "--python-package") {
      const char* value = require_value(arg);
      if (!value)
        return result;
      result.options.runtime.python_package = value;
      continue;
    }
    if (arg == "--nim-module") {
      const char* value = require_value(arg);
      if (!value)
        return result;
      result.options.runtime.nim_module = value;
      continue;
    }
    if (arg == "--nim-opaque") {
      const char* value = require_value(arg);
      if (!value)
        return result;
      result.options.runtime.nim_opaque = value;
      continue;
    }

    if (arg == "--opaque-types" || arg == "--zero-copy-substream") {
      const char* value = require_value(arg);
      if (!value) {
        return result;
      }
      std::string bool_value(value);
      if (!IsBoolValue(bool_value)) {
        result.status = ParseStatus::kError;
        result.message = "option expects boolean value 'true' or 'false': " + arg;
        return result;
      }
      const bool parsed = bool_value == "true";
      if (arg == "--opaque-types") {
        result.options.runtime.opaque_types = parsed;
      } else {
        result.options.runtime.zero_copy_substream = parsed;
      }
      continue;
    }

    if (arg == "--ksc-exceptions") {
      result.options.throw_exceptions = true;
      continue;
    }
    if (arg == "--ksc-json-output") {
      result.options.json_output = true;
      continue;
    }
    if (arg == "--verbose") {
      const char* value = require_value(arg);
      if (!value)
        return result;
      result.options.verbose.push_back(value);
      continue;
    }
    if (arg == "--no-auto-read") {
      result.options.runtime.auto_read = false;
      continue;
    }
    if (arg == "--read-pos") {
      result.options.runtime.read_pos = true;
      continue;
    }
    if (arg == "--debug") {
      result.options.runtime.auto_read = false;
      result.options.runtime.read_pos = true;
      continue;
    }

    if (arg == "--from-ir") {
      const char* value = require_value(arg);
      if (!value)
        return result;
      result.options.from_ir = value;
      continue;
    }

    if (!arg.empty() && arg[0] == '-') {
      if (!IsKnownOption(arg)) {
        result.status = ParseStatus::kError;
        result.message = "unknown option: " + arg;
        return result;
      }
    }

    result.options.src_files.push_back(arg);
  }

  if (result.options.runtime.read_write) {
    result.options.runtime.zero_copy_substream = false;
  }

  if (result.options.from_ir.empty() && result.options.targets.empty()) {
    result.status = ParseStatus::kError;
    result.message = "Missing required option --target";
    return result;
  }

  if (!result.options.from_ir.empty() && !result.options.src_files.empty()) {
    result.status = ParseStatus::kError;
    result.message = "--from-ir mode does not accept .ksy input files";
    return result;
  }

  return result;
}


std::string ValidateBackendCompatibility(const CliOptions& options) {
  if (options.targets.empty()) {
    return "";
  }

  if (options.targets.size() != 1) {
    return "multiple targets are not supported by compiler-cpp backend; specify exactly one target";
  }

  const std::string& target = options.targets.front();
  if (std::find(SupportedTargets().begin(), SupportedTargets().end(), target) ==
      SupportedTargets().end()) {
    return "target '" + target +
           "' is accepted by CLI but not implemented in compiler-cpp backend; supported targets are: " +
           Join(SupportedTargets(), ", ");
  }

  const bool is_cpp_stl = target == "cpp_stl";

  if (options.runtime.read_write && !SupportsReadWriteTarget(target)) {
    return "--read-write is not supported for target '" + target +
           "' in compiler-cpp backend; supported targets are: cpp_stl, python";
  }

  if (!options.runtime.auto_read && !(options.runtime.read_write || options.runtime.read_pos)) {
    return "--no-auto-read currently requires --read-write or --read-pos";
  }

  if (is_cpp_stl) {
    if (options.runtime.cpp_standard != "17") {
      return "target 'cpp_stl' currently requires --cpp-standard 17";
    }

    if (!options.runtime.python_package.empty()) {
      return "--python-package is only supported with target 'python'";
    }
    return "";
  }

  if (!options.runtime.cpp_namespace.empty()) {
    return "--cpp-namespace is only supported with target 'cpp_stl'";
  }
  if (!options.runtime.java_package.empty()) {
    return "--java-package is not supported for native compiler-cpp targets";
  }
  if (!options.runtime.java_from_file_class.empty()) {
    return "--java-from-file-class is not supported for native compiler-cpp targets";
  }
  if (!options.runtime.dotnet_namespace.empty()) {
    return "--dotnet-namespace is not supported for native compiler-cpp targets";
  }
  if (!options.runtime.php_namespace.empty()) {
    return "--php-namespace is not supported for native compiler-cpp targets";
  }
  if (!options.runtime.go_package.empty()) {
    return "--go-package is not supported for native compiler-cpp targets";
  }
  if (!options.runtime.nim_module.empty()) {
    return "--nim-module is not supported for native compiler-cpp targets";
  }
  if (!options.runtime.nim_opaque.empty()) {
    return "--nim-opaque is not supported for native compiler-cpp targets";
  }
  if (options.runtime.opaque_types) {
    return "--opaque-types is not supported for native compiler-cpp targets";
  }
  if (!options.runtime.zero_copy_substream && !options.runtime.read_write) {
    return "--zero-copy-substream=false is not supported for native compiler-cpp targets";
  }

  if (!options.runtime.python_package.empty() && target != "python") {
    return "--python-package is only supported with target 'python'";
  }
  return "";
}

} // namespace kscpp
