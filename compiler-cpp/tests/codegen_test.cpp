#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "cli_options.h"
#include "codegen.h"
#include "ir.h"

namespace {

bool Check(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    return false;
  }
  return true;
}

std::string ReadAll(const std::filesystem::path& p) {
  std::ifstream in(p);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

} // namespace

int main() {
  bool ok = true;

  {
    kscpp::ir::Spec spec;
    spec.name = "hello_world";
    spec.default_endian = kscpp::ir::Endian::kLe;

    kscpp::ir::Attr attr;
    attr.id = "one";
    attr.type.kind = kscpp::ir::TypeRef::Kind::kPrimitive;
    attr.type.primitive = kscpp::ir::PrimitiveType::kU1;
    spec.attrs.push_back(attr);

    kscpp::CliOptions options;
    const std::filesystem::path out = std::filesystem::temp_directory_path() / "kscpp_codegen_test";
    std::filesystem::remove_all(out);
    options.out_dir = out.string();
    options.targets = {"cpp_stl"};
    options.runtime.cpp_standard = "17";

    auto r1 = kscpp::codegen::EmitCppStl17FromIr(spec, options);
    ok &= Check(r1.ok, "minimal subset codegen succeeds");

    const auto header_path = out / "hello_world.h";
    const auto source_path = out / "hello_world.cpp";
    ok &= Check(std::filesystem::exists(header_path), "header emitted");
    ok &= Check(std::filesystem::exists(source_path), "source emitted");

    const std::string h1 = ReadAll(header_path);
    const std::string c1 = ReadAll(source_path);

    auto r2 = kscpp::codegen::EmitCppStl17FromIr(spec, options);
    ok &= Check(r2.ok, "second codegen run succeeds");
    ok &= Check(h1 == ReadAll(header_path), "header output is deterministic");
    ok &= Check(c1 == ReadAll(source_path), "source output is deterministic");
  }

  {
    kscpp::ir::Spec unsupported;
    unsupported.name = "unsupported";
    unsupported.default_endian = kscpp::ir::Endian::kLe;

    kscpp::ir::Instance inst;
    inst.id = "x";
    inst.value_expr = kscpp::ir::Expr::Int(1);
    unsupported.instances.push_back(inst);

    kscpp::CliOptions options;
    options.out_dir = (std::filesystem::temp_directory_path() / "kscpp_codegen_test_unsupported").string();

    auto r = kscpp::codegen::EmitCppStl17FromIr(unsupported, options);
    ok &= Check(!r.ok, "unsupported subset fails");
    ok &= Check(r.error.find("not yet supported") != std::string::npos,
                "unsupported subset has explicit diagnostic");
  }

  return ok ? 0 : 1;
}
