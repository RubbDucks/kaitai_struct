#include "codegen.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace kscpp::codegen {
namespace {

enum class ExprType {
  kInt8,
  kInt32,
  kBool,
};

std::string NormalizeOp(const std::string& op) {
  if (op == "and" || op == "&")
    return "&&";
  if (op == "or" || op == "|")
    return "||";
  if (op == "not")
    return "!";
  return op;
}

int ExprPrecedence(const ir::Expr& expr) {
  if (expr.kind != ir::Expr::Kind::kBinary) {
    return 100;
  }
  const std::string op = NormalizeOp(expr.text);
  if (op == "||") return 10;
  if (op == "&&") return 20;
  if (op == "==" || op == "!=") return 30;
  if (op == "<" || op == "<=" || op == ">" || op == ">=") return 40;
  if (op == "+" || op == "-") return 50;
  if (op == "*" || op == "/" || op == "%") return 60;
  return 5;
}

ExprType ExprResultType(const ir::Expr& expr, const std::set<std::string>& bool_instances,
                        const std::map<std::string, ExprType>& instance_types) {
  switch (expr.kind) {
  case ir::Expr::Kind::kBool:
    return ExprType::kBool;
  case ir::Expr::Kind::kInt:
    return (expr.int_value >= -128 && expr.int_value <= 127) ? ExprType::kInt8 : ExprType::kInt32;
  case ir::Expr::Kind::kName: {
    auto it = instance_types.find(expr.text);
    if (it != instance_types.end()) return it->second;
    return bool_instances.find(expr.text) != bool_instances.end() ? ExprType::kBool : ExprType::kInt32;
  }
  case ir::Expr::Kind::kUnary:
    return NormalizeOp(expr.text) == "!" ? ExprType::kBool : ExprType::kInt32;
  case ir::Expr::Kind::kBinary: {
    const auto op = NormalizeOp(expr.text);
    if (op == "&&" || op == "||" || op == "==" || op == "!=" || op == "<" || op == "<=" ||
        op == ">" || op == ">=") {
      return ExprType::kBool;
    }
    return ExprType::kInt32;
  }
  }
  return ExprType::kInt32;
}

std::string CppExprType(ExprType t) {
  if (t == ExprType::kBool) return "bool";
  if (t == ExprType::kInt8) return "int8_t";
  return "int32_t";
}

std::string RenderExpr(const ir::Expr& expr, const std::set<std::string>& attrs,
                       const std::set<std::string>& instances, int parent_prec) {
  switch (expr.kind) {
  case ir::Expr::Kind::kInt:
    return std::to_string(expr.int_value);
  case ir::Expr::Kind::kBool:
    return expr.bool_value ? "true" : "false";
  case ir::Expr::Kind::kName:
    if (attrs.find(expr.text) != attrs.end() || instances.find(expr.text) != instances.end()) return expr.text + "()";
    return expr.text;
  case ir::Expr::Kind::kUnary:
    return "(" + NormalizeOp(expr.text) + RenderExpr(*expr.lhs, attrs, instances, 90) + ")";
  case ir::Expr::Kind::kBinary: {
    const int prec = ExprPrecedence(expr);
    const std::string op = NormalizeOp(expr.text);
    std::string lhs = RenderExpr(*expr.lhs, attrs, instances, prec);
    std::string rhs = RenderExpr(*expr.rhs, attrs, instances, prec + 1);
    if (op == "&&" || op == "||") {
      lhs = "(" + lhs + ")";
      rhs = "(" + rhs + ")";
    }
    std::string rendered = lhs + " " + op + " " + rhs;
    if (op == "&&" || op == "||") {
      rendered = "(" + rendered + ")";
    } else if (prec <= parent_prec) {
      rendered = "(" + rendered + ")";
    }
    return rendered;
  }
  }
  return "0";
}

std::map<std::string, ExprType> ComputeInstanceTypes(const ir::Spec& spec) {
  std::set<std::string> bool_instances;
  std::map<std::string, ExprType> out;
  for (const auto& inst : spec.instances) {
    const auto ty = ExprResultType(inst.value_expr, bool_instances, out);
    out[inst.id] = ty;
    if (ty == ExprType::kBool) bool_instances.insert(inst.id);
  }
  return out;
}

Result ValidateSupportedSubset(const ir::Spec& spec) {
  if (!spec.types.empty()) return {false, "not yet supported: type definitions in IR (types section)"};
  if (!spec.validations.empty()) return {false, "not yet supported: validations in IR"};

  for (const auto& attr : spec.attrs) {
    if (attr.endian_override.has_value()) return {false, "not yet supported: attr.endian_override"};
    if (attr.size_expr.has_value()) return {false, "not yet supported: sized attrs via attr.size_expr"};
    if (attr.type.kind != ir::TypeRef::Kind::kPrimitive) return {false, "not yet supported: user-defined attr types"};
    switch (attr.type.primitive) {
    case ir::PrimitiveType::kU1: case ir::PrimitiveType::kU2: case ir::PrimitiveType::kU4: case ir::PrimitiveType::kU8:
    case ir::PrimitiveType::kS1: case ir::PrimitiveType::kS2: case ir::PrimitiveType::kS4: case ir::PrimitiveType::kS8:
      break;
    default:
      return {false, "not yet supported: primitive attr type in this migration slice"};
    }
  }

  std::set<std::string> known_names;
  for (const auto& attr : spec.attrs) known_names.insert(attr.id);

  std::function<Result(const ir::Expr&)> validate_expr = [&](const ir::Expr& expr) -> Result {
    switch (expr.kind) {
    case ir::Expr::Kind::kInt:
    case ir::Expr::Kind::kBool:
      return {true, ""};
    case ir::Expr::Kind::kName:
      if (known_names.find(expr.text) == known_names.end()) return {false, "not yet supported: expression name reference outside attrs/instances: " + expr.text};
      return {true, ""};
    case ir::Expr::Kind::kUnary:
      if (expr.text != "-" && expr.text != "!" && expr.text != "not") return {false, "not yet supported: unary operator \"" + expr.text + "\""};
      return validate_expr(*expr.lhs);
    case ir::Expr::Kind::kBinary: {
      static const std::set<std::string> supported_ops = {
          "+", "-", "*", "/", "%", "==", "!=", ">", ">=", "<", "<=", "&&", "||", "and", "or", "&", "|"};
      if (supported_ops.find(expr.text) == supported_ops.end()) return {false, "not yet supported: binary operator \"" + expr.text + "\""};
      auto lhs = validate_expr(*expr.lhs); if (!lhs.ok) return lhs; return validate_expr(*expr.rhs);
    }
    }
    return {false, "not yet supported: unknown expression kind"};
  };

  for (const auto& inst : spec.instances) {
    auto result = validate_expr(inst.value_expr);
    if (!result.ok) return result;
    known_names.insert(inst.id);
  }

  return {true, ""};
}

std::string CppFieldType(ir::PrimitiveType primitive) {
  switch (primitive) {
  case ir::PrimitiveType::kU1: return "uint8_t";
  case ir::PrimitiveType::kU2: return "uint16_t";
  case ir::PrimitiveType::kU4: return "uint32_t";
  case ir::PrimitiveType::kU8: return "uint64_t";
  case ir::PrimitiveType::kS1: return "int8_t";
  case ir::PrimitiveType::kS2: return "int16_t";
  case ir::PrimitiveType::kS4: return "int32_t";
  case ir::PrimitiveType::kS8: return "int64_t";
  default: return "uint8_t";
  }
}

std::string ReadMethod(ir::PrimitiveType primitive, ir::Endian endian) {
  const bool be = endian == ir::Endian::kBe;
  switch (primitive) {
  case ir::PrimitiveType::kU1: return "read_u1";
  case ir::PrimitiveType::kU2: return be ? "read_u2be" : "read_u2le";
  case ir::PrimitiveType::kU4: return be ? "read_u4be" : "read_u4le";
  case ir::PrimitiveType::kU8: return be ? "read_u8be" : "read_u8le";
  case ir::PrimitiveType::kS1: return "read_s1";
  case ir::PrimitiveType::kS2: return be ? "read_s2be" : "read_s2le";
  case ir::PrimitiveType::kS4: return be ? "read_s4be" : "read_s4le";
  case ir::PrimitiveType::kS8: return be ? "read_s8be" : "read_s8le";
  default: return "read_u1";
  }
}

std::string RenderHeader(const ir::Spec& spec) {
  const auto instance_types = ComputeInstanceTypes(spec);
  std::ostringstream out;
  out << "#pragma once\n\n";
  out << "// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild\n\n";
  out << "class " << spec.name << "_t;\n\n";
  out << "#include \"kaitai/kaitaistruct.h\"\n";
  out << "#include <stdint.h>\n";
  out << "#include <memory>\n\n";
  out << "#if KAITAI_STRUCT_VERSION < 11000L\n";
  out << "#error \"Incompatible Kaitai Struct C++/STL API: version 0.11 or later is required\"\n";
  out << "#endif\n\n";
  out << "class " << spec.name << "_t : public kaitai::kstruct {\n\n";
  out << "public:\n\n";
  out << "    " << spec.name << "_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent = nullptr, " << spec.name << "_t* p__root = nullptr);\n\n";
  out << "private:\n";
  out << "    void _read();\n";
  out << "    void _clean_up();\n\n";
  out << "public:\n";
  out << "    ~" << spec.name << "_t();\n";
  for (const auto& inst : spec.instances) out << "    " << CppExprType(instance_types.at(inst.id)) << " " << inst.id << "();\n";
  for (const auto& attr : spec.attrs) out << "    " << CppFieldType(attr.type.primitive) << " " << attr.id << "() const { return m_" << attr.id << "; }\n";
  out << "    " << spec.name << "_t* _root() const { return m__root; }\n";
  out << "    kaitai::kstruct* _parent() const { return m__parent; }\n\n";
  out << "private:\n";
  for (const auto& inst : spec.instances) {
    out << "    bool f_" << inst.id << ";\n";
    out << "    " << CppExprType(instance_types.at(inst.id)) << " m_" << inst.id << ";\n";
  }
  for (const auto& attr : spec.attrs) out << "    " << CppFieldType(attr.type.primitive) << " m_" << attr.id << ";\n";
  out << "    " << spec.name << "_t* m__root;\n";
  out << "    kaitai::kstruct* m__parent;\n";
  out << "};\n";
  return out.str();
}

std::string RenderSource(const ir::Spec& spec) {
  const auto instance_types = ComputeInstanceTypes(spec);
  std::set<std::string> attr_names;
  for (const auto& attr : spec.attrs) attr_names.insert(attr.id);

  std::ostringstream out;
  out << "// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild\n\n";
  out << "#include \"" << spec.name << ".h\"\n\n";
  out << spec.name << "_t::" << spec.name << "_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent, " << spec.name << "_t* p__root) : kaitai::kstruct(p__io) {\n";
  out << "    m__parent = p__parent;\n";
  out << "    m__root = p__root ? p__root : this;\n";
  for (const auto& inst : spec.instances) out << "    f_" << inst.id << " = false;\n";
  out << "    _read();\n";
  out << "}\n\n";

  out << "void " << spec.name << "_t::_read() {\n";
  for (const auto& attr : spec.attrs) out << "    m_" << attr.id << " = m__io->" << ReadMethod(attr.type.primitive, spec.default_endian) << "();\n";
  out << "}\n\n";

  out << spec.name << "_t::~" << spec.name << "_t() {\n";
  out << "    _clean_up();\n";
  out << "}\n\n";

  out << "void " << spec.name << "_t::_clean_up() {\n";
  out << "}\n";

  std::set<std::string> known_instances;
  for (const auto& inst : spec.instances) {
    out << "\n";
    out << CppExprType(instance_types.at(inst.id)) << " " << spec.name << "_t::" << inst.id << "() {\n";
    out << "    if (f_" << inst.id << ")\n";
    out << "        return m_" << inst.id << ";\n";
    out << "    f_" << inst.id << " = true;\n";
    std::string rendered = RenderExpr(inst.value_expr, attr_names, known_instances, -1);
    if (instance_types.at(inst.id) == ExprType::kBool) rendered = " " + rendered + " ";
    out << "    m_" << inst.id << " = " << rendered << ";\n";
    out << "    return m_" << inst.id << ";\n";
    out << "}\n";
    known_instances.insert(inst.id);
  }

  return out.str();
}

} // namespace

Result EmitCppStl17FromIr(const ir::Spec& spec, const CliOptions& options) {
  const auto validate_subset = ValidateSupportedSubset(spec);
  if (!validate_subset.ok) return validate_subset;

  const std::filesystem::path out_dir(options.out_dir);
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);
  if (ec) return {false, "failed to create output directory: " + ec.message()};

  const std::filesystem::path header_path = out_dir / (spec.name + ".h");
  const std::filesystem::path source_path = out_dir / (spec.name + ".cpp");

  std::ofstream header(header_path);
  if (!header) return {false, "failed to open output file: " + header_path.string()};
  std::ofstream source(source_path);
  if (!source) return {false, "failed to open output file: " + source_path.string()};

  header << RenderHeader(spec);
  source << RenderSource(spec);
  return {true, ""};
}

} // namespace kscpp::codegen
