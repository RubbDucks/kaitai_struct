#ifndef KAITAI_STRUCT_COMPILER_CPP_CODEGEN_H_
#define KAITAI_STRUCT_COMPILER_CPP_CODEGEN_H_

#include <string>

#include "cli_options.h"
#include "ir.h"

namespace kscpp::codegen {

struct Result {
  bool ok = false;
  std::string error;
};

Result EmitCppStl17FromIr(const ir::Spec& spec, const CliOptions& options);

} // namespace kscpp::codegen

#endif
