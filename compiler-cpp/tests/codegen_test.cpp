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
    kscpp::ir::Spec spec;
    spec.name = "expr_subset_a";
    spec.default_endian = kscpp::ir::Endian::kLe;

    kscpp::ir::Attr a;
    a.id = "a";
    a.type.kind = kscpp::ir::TypeRef::Kind::kPrimitive;
    a.type.primitive = kscpp::ir::PrimitiveType::kU1;
    spec.attrs.push_back(a);

    kscpp::ir::Attr b;
    b.id = "b";
    b.type.kind = kscpp::ir::TypeRef::Kind::kPrimitive;
    b.type.primitive = kscpp::ir::PrimitiveType::kU1;
    spec.attrs.push_back(b);

    kscpp::ir::Instance lit;
    lit.id = "lit";
    lit.value_expr = kscpp::ir::Expr::Int(7);
    spec.instances.push_back(lit);

    kscpp::ir::Instance arith;
    arith.id = "arith";
    arith.value_expr = kscpp::ir::Expr::Binary(
        "-", kscpp::ir::Expr::Binary("+", kscpp::ir::Expr::Name("a"),
                                      kscpp::ir::Expr::Binary("*", kscpp::ir::Expr::Name("b"),
                                                              kscpp::ir::Expr::Int(3))),
        kscpp::ir::Expr::Int(2));
    spec.instances.push_back(arith);

    kscpp::ir::Instance logic;
    logic.id = "logic";
    logic.value_expr =
        kscpp::ir::Expr::Binary("and", kscpp::ir::Expr::Binary(">", kscpp::ir::Expr::Name("a"),
                                                                 kscpp::ir::Expr::Name("b")),
                                kscpp::ir::Expr::Binary("==", kscpp::ir::Expr::Name("lit"),
                                                        kscpp::ir::Expr::Int(7)));
    spec.instances.push_back(logic);

    kscpp::ir::Instance ref_mix;
    ref_mix.id = "ref_mix";
    ref_mix.value_expr =
        kscpp::ir::Expr::Binary("+", kscpp::ir::Expr::Name("lit"), kscpp::ir::Expr::Name("a"));
    spec.instances.push_back(ref_mix);

    kscpp::CliOptions options;
    const std::filesystem::path out = std::filesystem::temp_directory_path() / "kscpp_codegen_expr_test";
    std::filesystem::remove_all(out);
    options.out_dir = out.string();
    options.targets = {"cpp_stl"};
    options.runtime.cpp_standard = "17";

    auto r = kscpp::codegen::EmitCppStl17FromIr(spec, options);
    ok &= Check(r.ok, "expression subset A codegen succeeds");

    const std::string h = ReadAll(out / "expr_subset_a.h");
    const std::string c = ReadAll(out / "expr_subset_a.cpp");
    ok &= Check(h.find("int32_t arith();") != std::string::npos,
                "arith instance accessor emitted");
    ok &= Check(h.find("bool logic();") != std::string::npos, "logic bool accessor emitted");
    ok &= Check(c.find("m_arith = (a() + b() * 3) - 2;") != std::string::npos,
                "arithmetic precedence preserved without over-parenthesizing");
    ok &= Check(c.find("m_logic =  ((a() > b()) && (lit() == 7)) ;") != std::string::npos,
                "boolean grouping/parenthesization emitted");
    ok &= Check(c.find("m_ref_mix = lit() + a();") != std::string::npos,
                "instance-to-instance and field refs emitted");
  }

  {
    kscpp::ir::Spec unsupported;
    unsupported.name = "unsupported";
    unsupported.default_endian = kscpp::ir::Endian::kLe;

    kscpp::ir::Validation validation;
    validation.target = "one";
    validation.condition_expr = kscpp::ir::Expr::Bool(true);
    validation.message = "todo";
    unsupported.validations.push_back(validation);

    kscpp::CliOptions options;
    options.out_dir = (std::filesystem::temp_directory_path() / "kscpp_codegen_test_unsupported").string();

    auto r = kscpp::codegen::EmitCppStl17FromIr(unsupported, options);
    ok &= Check(!r.ok, "unsupported subset fails");
    ok &= Check(r.error.find("not yet supported") != std::string::npos,
                "unsupported subset has explicit diagnostic");
  }

  return ok ? 0 : 1;
}
