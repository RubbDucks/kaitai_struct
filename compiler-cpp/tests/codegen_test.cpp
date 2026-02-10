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

    kscpp::ir::Instance bitwise;
    bitwise.id = "bitwise";
    bitwise.value_expr = kscpp::ir::Expr::Binary("|",
        kscpp::ir::Expr::Binary("<<", kscpp::ir::Expr::Name("a"), kscpp::ir::Expr::Int(2)),
        kscpp::ir::Expr::Unary("~", kscpp::ir::Expr::Name("b")));
    spec.instances.push_back(bitwise);

    auto r_bitwise = kscpp::codegen::EmitCppStl17FromIr(spec, options);
    ok &= Check(r_bitwise.ok, "extended bitwise expression codegen succeeds");
    const std::string c_bitwise = ReadAll(out / "expr_subset_a.cpp");
    ok &= Check(c_bitwise.find("m_bitwise =") != std::string::npos &&
                c_bitwise.find("<< 2") != std::string::npos &&
                c_bitwise.find("~b()") != std::string::npos &&
                c_bitwise.find("|") != std::string::npos,
                "bitwise/shift/unary-not operators emitted");
  }


  {
    kscpp::ir::Spec spec;
    spec.name = "type_subset";
    spec.default_endian = kscpp::ir::Endian::kLe;

    kscpp::ir::EnumDef e;
    e.name = "animal";
    e.values.push_back({7, "cat"});
    e.values.push_back({13, "dog"});
    spec.enums.push_back(e);

    auto add_attr = [&](const std::string& id, kscpp::ir::PrimitiveType t) {
      kscpp::ir::Attr a;
      a.id = id;
      a.type.kind = kscpp::ir::TypeRef::Kind::kPrimitive;
      a.type.primitive = t;
      spec.attrs.push_back(a);
    };

    add_attr("u8v", kscpp::ir::PrimitiveType::kU8);
    add_attr("s4v", kscpp::ir::PrimitiveType::kS4);
    add_attr("f4v", kscpp::ir::PrimitiveType::kF4);
    add_attr("f8v", kscpp::ir::PrimitiveType::kF8);

    kscpp::ir::Attr bytes;
    bytes.id = "payload";
    bytes.type.kind = kscpp::ir::TypeRef::Kind::kPrimitive;
    bytes.type.primitive = kscpp::ir::PrimitiveType::kBytes;
    bytes.size_expr = kscpp::ir::Expr::Int(4);
    spec.attrs.push_back(bytes);

    kscpp::ir::Attr str;
    str.id = "name";
    str.type.kind = kscpp::ir::TypeRef::Kind::kPrimitive;
    str.type.primitive = kscpp::ir::PrimitiveType::kStr;
    str.size_expr = kscpp::ir::Expr::Int(3);
    str.encoding = "ASCII";
    spec.attrs.push_back(str);

    kscpp::ir::Attr en;
    en.id = "pet";
    en.type.kind = kscpp::ir::TypeRef::Kind::kPrimitive;
    en.type.primitive = kscpp::ir::PrimitiveType::kU1;
    en.enum_name = "animal";
    spec.attrs.push_back(en);

    kscpp::CliOptions options;
    const std::filesystem::path out = std::filesystem::temp_directory_path() / "kscpp_codegen_type_test";
    std::filesystem::remove_all(out);
    options.out_dir = out.string();
    options.targets = {"cpp_stl"};
    options.runtime.cpp_standard = "17";

    auto r = kscpp::codegen::EmitCppStl17FromIr(spec, options);
    ok &= Check(r.ok, "type subset codegen succeeds");

    const std::string h = ReadAll(out / "type_subset.h");
    const std::string c = ReadAll(out / "type_subset.cpp");
    ok &= Check(h.find("enum class animal_e") != std::string::npos, "enum emitted");
    ok &= Check(h.find("double f8v() const") != std::string::npos, "float64 accessor emitted");
    ok &= Check(h.find("const std::string& payload() const") != std::string::npos, "bytes accessor emitted");
    ok &= Check(c.find("m_f4v = m__io->read_f4le();") != std::string::npos, "f4 read emitted");
    ok &= Check(c.find("m_payload = m__io->read_bytes(4);") != std::string::npos, "bytes read emitted");
    ok &= Check(c.find("read_bytes(3)") != std::string::npos && c.find("ASCII") != std::string::npos, "encoded string read emitted");
    ok &= Check(c.find("m_pet = static_cast<animal_e>(m__io->read_u1());") != std::string::npos, "enum cast emitted");
  }


  {
    kscpp::ir::Spec spec;
    auto parsed = kscpp::ir::LoadFromFile("../tests/data/control_flow_subset.ksir", &spec);
    ok &= Check(parsed.ok, "control-flow fixture parses");

    kscpp::CliOptions options;
    const std::filesystem::path out = std::filesystem::temp_directory_path() / "kscpp_codegen_control_flow_test";
    std::filesystem::remove_all(out);
    options.out_dir = out.string();
    options.targets = {"cpp_stl"};
    options.runtime.cpp_standard = "17";

    auto r = kscpp::codegen::EmitCppStl17FromIr(spec, options);
    ok &= Check(r.ok, "control-flow subset codegen succeeds");

    const std::string h = ReadAll(out / "control_flow_subset.h");
    const std::string c = ReadAll(out / "control_flow_subset.cpp");
    ok &= Check(h.find("std::vector<uint8_t>") != std::string::npos, "repeat attrs use vector storage");
    ok &= Check(c.find("while (!m__io->is_eof())") != std::string::npos, "repeat-eos emitted");
    ok &= Check(c.find("for (int i = 0; i < 2; i++)") != std::string::npos, "repeat-expr emitted");
    ok &= Check(c.find("do {") != std::string::npos && c.find("repeat_item == 255") != std::string::npos,
                "repeat-until emitted");
    ok &= Check(c.find("if (opcode() == 1)") != std::string::npos, "if-conditional field emitted");
    ok &= Check(c.find("if (opcode() == 1)") != std::string::npos && c.find("if (opcode() == 2)") != std::string::npos,
                "switch-on cases emitted");
  }

  {
    kscpp::ir::Spec bad;
    auto parsed = kscpp::ir::LoadFromFile("../tests/data/invalid_switch_duplicate_else.ksir", &bad);
    ok &= Check(!parsed.ok, "malformed switch duplicate else rejected deterministically");
  }

  {
    kscpp::ir::Spec spec;
    auto parsed = kscpp::ir::LoadFromFile("../tests/data/unsupported_dynamic_switch.ksir", &spec);
    ok &= Check(parsed.ok, "dynamic switch fixture parses");

    kscpp::CliOptions options;
    const std::filesystem::path out = std::filesystem::temp_directory_path() / "kscpp_codegen_dynamic_switch";
    std::filesystem::remove_all(out);
    options.out_dir = out.string();
    options.targets = {"cpp_stl"};
    options.runtime.cpp_standard = "17";

    auto r = kscpp::codegen::EmitCppStl17FromIr(spec, options);
    ok &= Check(r.ok, "dynamic switch and user type fixture codegen succeeds");

    const std::string h = ReadAll(out / "unsupported_dynamic_switch.h");
    const std::string c = ReadAll(out / "unsupported_dynamic_switch.cpp");
    ok &= Check(h.find("uint16_t tagged() const") != std::string::npos,
                "user-defined attr types resolve to primitive storage");
    ok &= Check(c.find("if (tag() + 1 == tag() - 1)") != std::string::npos,
                "dynamic switch-on expression emitted");
    ok &= Check(c.find("if (tag() + 1 == tag() + 1)") != std::string::npos,
                "switch case expression supports richer expressions");
  }
  {
    kscpp::ir::Spec spec;
    auto parsed = kscpp::ir::LoadFromFile("../tests/data/advanced_semantics_subset.ksir", &spec);
    ok &= Check(parsed.ok, "advanced semantics fixture parses");

    kscpp::CliOptions options;
    const std::filesystem::path out = std::filesystem::temp_directory_path() / "kscpp_codegen_advanced_semantics";
    std::filesystem::remove_all(out);
    options.out_dir = out.string();
    options.targets = {"cpp_stl"};
    options.runtime.cpp_standard = "17";

    auto r = kscpp::codegen::EmitCppStl17FromIr(spec, options);
    ok &= Check(r.ok, "advanced semantics subset codegen succeeds");

    const std::string h = ReadAll(out / "advanced_semantics_subset.h");
    const std::string c = ReadAll(out / "advanced_semantics_subset.cpp");
    ok &= Check(h.find("payload_len();") != std::string::npos && h.find("is_flag_one();") != std::string::npos,
                "instance accessors emitted for advanced fixture");
    ok &= Check(c.find("process_xor_one") != std::string::npos,
                "process xor const emitted");
    ok &= Check(c.find("validation_expr_error<uint8_t>") != std::string::npos,
                "attr validation emitted as validation_expr_error");
    ok &= Check(c.find("validation_expr_error<bool>") != std::string::npos,
                "instance validation emitted as validation_expr_error");
    ok &= Check(c.find("/valid/len") != std::string::npos && c.find("/valid/is_flag_one") != std::string::npos,
                "validation source paths are emitted");
  }


  {
    kscpp::ir::Spec spec;
    spec.name = "script_target_smoke";
    spec.default_endian = kscpp::ir::Endian::kLe;

    kscpp::ir::Attr attr;
    attr.id = "one";
    attr.type.kind = kscpp::ir::TypeRef::Kind::kPrimitive;
    attr.type.primitive = kscpp::ir::PrimitiveType::kU1;
    spec.attrs.push_back(attr);

    kscpp::ir::Attr payload;
    payload.id = "payload";
    payload.type.kind = kscpp::ir::TypeRef::Kind::kPrimitive;
    payload.type.primitive = kscpp::ir::PrimitiveType::kBytes;
    payload.size_expr = kscpp::ir::Expr::Int(2);
    kscpp::ir::Attr::Process process;
    process.kind = kscpp::ir::Attr::Process::Kind::kXorConst;
    process.xor_const = 255;
    payload.process = process;
    spec.attrs.push_back(payload);

    kscpp::ir::Instance inst;
    inst.id = "is_nonzero";
    inst.value_expr = kscpp::ir::Expr::Binary("!=", kscpp::ir::Expr::Name("one"), kscpp::ir::Expr::Int(0));
    spec.instances.push_back(inst);

    kscpp::ir::Validation val;
    val.target = "one";
    val.condition_expr = kscpp::ir::Expr::Binary("!=", kscpp::ir::Expr::Name("one"), kscpp::ir::Expr::Int(0));
    val.message = "one must be non-zero";
    spec.validations.push_back(val);

    const std::filesystem::path out = std::filesystem::temp_directory_path() / "kscpp_codegen_script_target_test";
    std::filesystem::remove_all(out);

    {
      kscpp::CliOptions options;
      options.out_dir = out.string();
      auto r = kscpp::codegen::EmitLuaFromIr(spec, options);
      ok &= Check(r.ok, "lua codegen succeeds");
      ok &= Check(std::filesystem::exists(out / "script_target_smoke.lua"), "lua module emitted");
      const auto text = ReadAll(out / "script_target_smoke.lua");
      ok &= Check(text.find("KaitaiStream.process_xor_one") != std::string::npos,
                  "lua output maps process_xor_const to runtime API");
      auto r2 = kscpp::codegen::EmitLuaFromIr(spec, options);
      ok &= Check(r2.ok && text == ReadAll(out / "script_target_smoke.lua"),
                  "lua output is deterministic");
    }

    {
      kscpp::CliOptions options;
      options.out_dir = out.string();
      auto r = kscpp::codegen::EmitWiresharkLuaFromIr(spec, options);
      ok &= Check(r.ok, "wireshark_lua codegen succeeds");
      ok &= Check(ReadAll(out / "script_target_smoke.lua").find("class.class") != std::string::npos,
                  "wireshark lua output emits parser class body");
    }

    {
      kscpp::CliOptions options;
      options.out_dir = out.string();
      options.runtime.python_package = "pkg.subpkg";
      auto r = kscpp::codegen::EmitPythonFromIr(spec, options);
      ok &= Check(r.ok, "python codegen succeeds");
      const auto py_path = out / "pkg" / "subpkg" / "script_target_smoke.py";
      ok &= Check(std::filesystem::exists(py_path), "python module emitted under package path");
      const auto text = ReadAll(py_path);
      ok &= Check(text.find("class ScriptTargetSmoke(KaitaiStruct)") != std::string::npos,
                  "python parser class emitted");
      ok &= Check(text.find("ValidationExprError") != std::string::npos,
                  "python valid-expression support emitted");
      ok &= Check(text.find("@property") != std::string::npos,
                  "python instances emitted as cached properties");
      auto r2 = kscpp::codegen::EmitPythonFromIr(spec, options);
      ok &= Check(r2.ok && text == ReadAll(py_path), "python output is deterministic");
    }

    {
      kscpp::CliOptions options;
      options.out_dir = out.string();
      auto r = kscpp::codegen::EmitRubyFromIr(spec, options);
      ok &= Check(r.ok, "ruby codegen succeeds");
      ok &= Check(std::filesystem::exists(out / "script_target_smoke.rb"), "ruby module emitted");
      const auto text = ReadAll(out / "script_target_smoke.rb");
      ok &= Check(text.find("class ScriptTargetSmoke < Kaitai::Struct::Struct") != std::string::npos,
                  "ruby parser class emitted");
      ok &= Check(text.find("ValidationExprError") != std::string::npos,
                  "ruby valid-expression support emitted");
      auto r2 = kscpp::codegen::EmitRubyFromIr(spec, options);
      ok &= Check(r2.ok && text == ReadAll(out / "script_target_smoke.rb"),
                  "ruby output is deterministic");
    }
  }

  {
    kscpp::ir::Spec unsupported;
    unsupported.name = "unsupported";
    unsupported.default_endian = kscpp::ir::Endian::kLe;

    kscpp::ir::Validation validation;
    validation.target = "missing";
    validation.condition_expr = kscpp::ir::Expr::Bool(true);
    validation.message = "todo";
    unsupported.validations.push_back(validation);

    kscpp::CliOptions options;
    options.out_dir = (std::filesystem::temp_directory_path() / "kscpp_codegen_test_unsupported").string();

    auto r = kscpp::codegen::EmitCppStl17FromIr(unsupported, options);
    ok &= Check(!r.ok, "unsupported validation target fails");
    ok &= Check(r.error.find("validation target outside attrs/instances") != std::string::npos,
                "unsupported validation target has explicit diagnostic");
  }

  {
    kscpp::ir::Spec unsupported_expr;
    unsupported_expr.name = "unsupported_expr";
    unsupported_expr.default_endian = kscpp::ir::Endian::kLe;

    kscpp::ir::Attr one;
    one.id = "one";
    one.type.kind = kscpp::ir::TypeRef::Kind::kPrimitive;
    one.type.primitive = kscpp::ir::PrimitiveType::kU1;
    unsupported_expr.attrs.push_back(one);

    kscpp::ir::Instance bad_op;
    bad_op.id = "bad_op";
    bad_op.value_expr = kscpp::ir::Expr::Binary("**", kscpp::ir::Expr::Name("one"), kscpp::ir::Expr::Int(2));
    unsupported_expr.instances.push_back(bad_op);

    kscpp::CliOptions options;
    options.out_dir = (std::filesystem::temp_directory_path() / "kscpp_codegen_test_unsupported_expr").string();

    auto r = kscpp::codegen::EmitCppStl17FromIr(unsupported_expr, options);
    ok &= Check(!r.ok, "unsupported expression operator fails");
    ok &= Check(r.error.find("binary operator \"**\"") != std::string::npos,
                "unsupported expression operator has explicit diagnostic");
  }

  return ok ? 0 : 1;
}
