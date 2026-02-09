#include "codegen.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace kscpp::codegen {
namespace {

bool EnumNameMatches(const std::string& declared, const std::string& ref);


std::optional<ir::PrimitiveType> ResolvePrimitiveType(
    const ir::TypeRef& ref,
    const std::map<std::string, ir::TypeRef>& user_types) {
  if (ref.kind == ir::TypeRef::Kind::kPrimitive) return ref.primitive;

  std::set<std::string> seen;
  std::string cur = ref.user_type;
  while (true) {
    auto it = user_types.find(cur);
    if (it == user_types.end()) return std::nullopt;
    if (!seen.insert(cur).second) return std::nullopt;
    if (it->second.kind == ir::TypeRef::Kind::kPrimitive) return it->second.primitive;
    cur = it->second.user_type;
  }
}

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
                       const std::set<std::string>& instances, int parent_prec,
                       const std::string& repeat_item_name = "") {
  switch (expr.kind) {
  case ir::Expr::Kind::kInt:
    return std::to_string(expr.int_value);
  case ir::Expr::Kind::kBool:
    return expr.bool_value ? "true" : "false";
  case ir::Expr::Kind::kName:
    if (!repeat_item_name.empty() && expr.text == "_") return repeat_item_name;
    if (attrs.find(expr.text) != attrs.end() || instances.find(expr.text) != instances.end()) return expr.text + "()";
    return expr.text;
  case ir::Expr::Kind::kUnary:
    return "(" + NormalizeOp(expr.text) + RenderExpr(*expr.lhs, attrs, instances, 90, repeat_item_name) + ")";
  case ir::Expr::Kind::kBinary: {
    const int prec = ExprPrecedence(expr);
    const std::string op = NormalizeOp(expr.text);
    std::string lhs = RenderExpr(*expr.lhs, attrs, instances, prec, repeat_item_name);
    std::string rhs = RenderExpr(*expr.rhs, attrs, instances, prec + 1, repeat_item_name);
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

std::map<std::string, ir::TypeRef> BuildUserTypeMap(const ir::Spec& spec) {
  std::map<std::string, ir::TypeRef> user_types;
  for (const auto& type : spec.types) {
    user_types[type.name] = type.type;
  }
  return user_types;
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
  std::map<std::string, ir::TypeRef> user_types;
  for (const auto& type : spec.types) {
    user_types[type.name] = type.type;
  }

  std::function<std::optional<ir::PrimitiveType>(const ir::TypeRef&)> resolve_primitive =
      [&](const ir::TypeRef& ref) -> std::optional<ir::PrimitiveType> {
    if (ref.kind == ir::TypeRef::Kind::kPrimitive) return ref.primitive;
    std::set<std::string> seen;
    std::string cur = ref.user_type;
    while (true) {
      auto it = user_types.find(cur);
      if (it == user_types.end()) return std::nullopt;
      if (!seen.insert(cur).second) return std::nullopt;
      if (it->second.kind == ir::TypeRef::Kind::kPrimitive) return it->second.primitive;
      cur = it->second.user_type;
    }
  };

  for (const auto& attr : spec.attrs) {
    auto resolved_type = resolve_primitive(attr.type);
    if (!resolved_type.has_value()) return {false, "not yet supported: attr type must resolve to primitive type"};
    if (attr.encoding.has_value() && resolved_type.value() != ir::PrimitiveType::kStr) {
      return {false, "not yet supported: encoding outside str attrs"};
    }
    switch (resolved_type.value()) {
    case ir::PrimitiveType::kU1: case ir::PrimitiveType::kU2: case ir::PrimitiveType::kU4: case ir::PrimitiveType::kU8:
    case ir::PrimitiveType::kS1: case ir::PrimitiveType::kS2: case ir::PrimitiveType::kS4: case ir::PrimitiveType::kS8:
    case ir::PrimitiveType::kF4: case ir::PrimitiveType::kF8: case ir::PrimitiveType::kBytes: case ir::PrimitiveType::kStr:
      break;
    default:
      return {false, "not yet supported: primitive attr type in this migration slice"};
    }
  }


  std::vector<std::string> declared_enums;
  for (const auto& e : spec.enums) {
    if (e.name.empty()) return {false, "not yet supported: empty enum name"};
    declared_enums.push_back(e.name);
  }

  for (const auto& attr : spec.attrs) {
    auto resolved_type = resolve_primitive(attr.type);
    if (!resolved_type.has_value()) return {false, "not yet supported: attr type must resolve to primitive type"};
    if (attr.switch_on.has_value()) {
      std::optional<ir::PrimitiveType> switch_case_type;
      bool has_else = false;
      for (const auto& c : attr.switch_cases) {
        auto resolved_case = resolve_primitive(c.type);
        if (!resolved_case.has_value()) {
          return {false, "not yet supported: switch-on case type must resolve to primitive type"};
        }
        if (!switch_case_type.has_value()) switch_case_type = resolved_case.value();
        if (switch_case_type.value() != resolved_case.value()) {
          return {false, "not yet supported: switch-on cases must share one primitive type"};
        }
        if (!c.match_expr.has_value()) {
          if (has_else) return {false, "not yet supported: malformed switch cases (duplicate else)"};
          has_else = true;
        }
      }
    }
    if (attr.enum_name.has_value()) {
      bool known_enum = false;
      for (const auto& e_name : declared_enums) {
        if (EnumNameMatches(e_name, *attr.enum_name)) { known_enum = true; break; }
      }
      if (!known_enum) {
        return {false, "not yet supported: attr.enum_name references unknown enum"};
      }
      switch (resolved_type.value()) {
      case ir::PrimitiveType::kU1: case ir::PrimitiveType::kU2: case ir::PrimitiveType::kU4: case ir::PrimitiveType::kU8:
      case ir::PrimitiveType::kS1: case ir::PrimitiveType::kS2: case ir::PrimitiveType::kS4: case ir::PrimitiveType::kS8:
        break;
      default:
        return {false, "not yet supported: enum attrs must be integer-backed"};
      }
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
      if (expr.text != "_" && known_names.find(expr.text) == known_names.end()) return {false, "not yet supported: expression name reference outside attrs/instances: " + expr.text};
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

  for (const auto& validation : spec.validations) {
    if (known_names.find(validation.target) == known_names.end()) {
      return {false, "not yet supported: validation target outside attrs/instances: " + validation.target};
    }
    auto result = validate_expr(validation.condition_expr);
    if (!result.ok) return result;
  }

  for (const auto& attr : spec.attrs) {
    if (attr.if_expr.has_value()) {
      auto result = validate_expr(*attr.if_expr);
      if (!result.ok) return result;
    }
    if (attr.size_expr.has_value()) {
      auto result = validate_expr(*attr.size_expr);
      if (!result.ok) return result;
    }
    if (attr.repeat_expr.has_value()) {
      auto result = validate_expr(*attr.repeat_expr);
      if (!result.ok) return result;
    }
    if (attr.switch_on.has_value()) {
      auto result = validate_expr(*attr.switch_on);
      if (!result.ok) return result;
    }
    for (const auto& c : attr.switch_cases) {
      if (!c.match_expr.has_value()) continue;
      auto result = validate_expr(*c.match_expr);
      if (!result.ok) return result;
    }
  }

  return {true, ""};
}

bool EnumNameMatches(const std::string& declared, const std::string& ref) {
  if (declared == ref) return true;
  if (declared.size() > ref.size() && declared.compare(declared.size() - ref.size(), ref.size(), ref) == 0 && declared[declared.size() - ref.size() - 1] == ':') return true;
  return false;
}

std::string EnumCppTypeName(const std::string& enum_name) {
  const auto pos = enum_name.rfind("::");
  const std::string base = (pos == std::string::npos) ? enum_name : enum_name.substr(pos + 2);
  std::string out;
  out.reserve(base.size() + 2);
  for (char c : base) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  if (out.empty() || (out[0] >= '0' && out[0] <= '9')) out.insert(out.begin(), '_');
  return out + "_e";
}

std::string EnumValueName(const std::string& name) {
  std::string out;
  out.reserve(name.size() + 1);
  for (char c : name) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  if (out.empty() || (out[0] >= '0' && out[0] <= '9')) out.insert(out.begin(), '_');
  return out;
}

std::string CppFieldType(ir::PrimitiveType primitive);
std::string ReadMethod(ir::PrimitiveType primitive, ir::Endian endian);

std::string SwitchCaseType(const ir::Attr& attr, const std::map<std::string, ir::TypeRef>& user_types) {
  if (attr.switch_cases.empty()) {
    auto primitive = ResolvePrimitiveType(attr.type, user_types);
    return CppFieldType(primitive.value_or(ir::PrimitiveType::kU1));
  }
  const auto primitive = ResolvePrimitiveType(attr.switch_cases.front().type, user_types);
  return CppFieldType(primitive.value_or(ir::PrimitiveType::kU1));
}

std::string CppAttrType(const ir::Attr& attr, const std::map<std::string, ir::TypeRef>& user_types) {
  if (attr.enum_name.has_value()) return EnumCppTypeName(*attr.enum_name);
  const auto primitive = ResolvePrimitiveType(attr.type, user_types);
  return CppFieldType(primitive.value_or(ir::PrimitiveType::kU1));
}

std::string CppReadPrimitiveExpr(ir::PrimitiveType primitive, std::optional<ir::Endian> override_endian,
                                 ir::Endian default_endian) {
  if (primitive == ir::PrimitiveType::kBytes) return "m__io->read_bytes_full()";
  if (primitive == ir::PrimitiveType::kStr) return "std::string()";
  return "m__io->" + ReadMethod(primitive, override_endian.value_or(default_endian)) + "()";
}

std::string ReadExpr(const ir::Attr& attr, ir::Endian default_endian,
                     const std::map<std::string, ir::TypeRef>& user_types) {
  const auto primitive = ResolvePrimitiveType(attr.type, user_types).value_or(ir::PrimitiveType::kU1);
  if (primitive == ir::PrimitiveType::kBytes) {
    std::string read = attr.size_expr.has_value() ?
      ("m__io->read_bytes(" + RenderExpr(*attr.size_expr, {}, {}, -1) + ")") :
      "m__io->read_bytes_full()";
    if (attr.process.has_value() && attr.process->kind == ir::Attr::Process::Kind::kXorConst) {
      read = "kaitai::kstream::process_xor_one(" + read + ", " + std::to_string(attr.process->xor_const) + ")";
    }
    return read;
  }
  if (primitive == ir::PrimitiveType::kStr) {
    if (!attr.size_expr.has_value()) return "std::string()";
    const std::string enc = attr.encoding.value_or("UTF-8");
    return "kaitai::kstream::bytes_to_str(m__io->read_bytes(" + RenderExpr(*attr.size_expr, {}, {}, -1) + "), \"" + enc + "\")";
  }
  std::string base = CppReadPrimitiveExpr(primitive, attr.endian_override, default_endian);
  if (attr.enum_name.has_value()) return "static_cast<" + EnumCppTypeName(*attr.enum_name) + ">(" + base + ")";
  return base;
}

std::string ReadSwitchExpr(const ir::Attr& attr, ir::Endian default_endian,
                           const std::set<std::string>& attrs, const std::set<std::string>& instances,
                           const std::map<std::string, ir::TypeRef>& user_types) {
  const std::string on = RenderExpr(*attr.switch_on, attrs, instances, -1);
  std::ostringstream out;
  out << "([&]() {\n";
  bool has_else = false;
  for (const auto& c : attr.switch_cases) {
    if (!c.match_expr.has_value()) continue;
    const auto case_primitive = ResolvePrimitiveType(c.type, user_types).value_or(ir::PrimitiveType::kU1);
    out << "        if (" << on << " == " << RenderExpr(*c.match_expr, attrs, instances, -1)
        << ") return " << CppReadPrimitiveExpr(case_primitive, attr.endian_override, default_endian) << ";\n";
  }
  for (const auto& c : attr.switch_cases) {
    if (c.match_expr.has_value()) continue;
    const auto case_primitive = ResolvePrimitiveType(c.type, user_types).value_or(ir::PrimitiveType::kU1);
    out << "        return " << CppReadPrimitiveExpr(case_primitive, attr.endian_override, default_endian) << ";\n";
    has_else = true;
    break;
  }
  if (!has_else) out << "        throw std::runtime_error(\"switch-on has no matching case\");\n";
  out << "    })()";
  return out.str();
}

std::string CppStorageType(const ir::Attr& attr, const std::map<std::string, ir::TypeRef>& user_types) {
  std::string base = attr.switch_on.has_value() ? SwitchCaseType(attr, user_types) : CppAttrType(attr, user_types);
  if (attr.repeat != ir::Attr::RepeatKind::kNone) return "std::vector<" + base + ">";
  return base;
}

std::string CppAccessorType(const ir::Attr& attr, const std::map<std::string, ir::TypeRef>& user_types) {
  const std::string storage = CppStorageType(attr, user_types);
  if (attr.repeat != ir::Attr::RepeatKind::kNone) return "const " + storage + "&";
  if (attr.type.primitive == ir::PrimitiveType::kBytes || attr.type.primitive == ir::PrimitiveType::kStr) {
    return "const " + storage + "&";
  }
  return storage;
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
  case ir::PrimitiveType::kF4: return "float";
  case ir::PrimitiveType::kF8: return "double";
  case ir::PrimitiveType::kStr: return "std::string";
  case ir::PrimitiveType::kBytes: return "std::string";
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
  case ir::PrimitiveType::kF4: return be ? "read_f4be" : "read_f4le";
  case ir::PrimitiveType::kF8: return be ? "read_f8be" : "read_f8le";
  default: return "read_u1";
  }
}

bool NeedsVectorInclude(const ir::Spec& spec) {
  for (const auto& attr : spec.attrs) {
    if (attr.repeat != ir::Attr::RepeatKind::kNone) return true;
  }
  return false;
}

bool NeedsStringInclude(const ir::Spec& spec, const std::map<std::string, ir::TypeRef>& user_types) {
  for (const auto& attr : spec.attrs) {
    const auto primitive = ResolvePrimitiveType(attr.type, user_types);
    if (primitive.has_value() &&
        (primitive.value() == ir::PrimitiveType::kStr || primitive.value() == ir::PrimitiveType::kBytes)) {
      return true;
    }
  }
  return false;
}

std::string RenderHeader(const ir::Spec& spec) {
  const auto instance_types = ComputeInstanceTypes(spec);
  const auto user_types = BuildUserTypeMap(spec);
  std::ostringstream out;
  out << "#pragma once\n\n";
  out << "// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild\n\n";
  out << "class " << spec.name << "_t;\n\n";
  out << "#include \"kaitai/kaitaistruct.h\"\n";
  out << "#include <kaitai/exceptions.h>\n";
  out << "#include <stdint.h>\n";
  out << "#include <memory>\n";
  if (NeedsStringInclude(spec, user_types)) out << "#include <string>\n";
  if (NeedsVectorInclude(spec)) out << "#include <vector>\n";
  out << "\n";
  out << "#if KAITAI_STRUCT_VERSION < 11000L\n";
  out << "#error \"Incompatible Kaitai Struct C++/STL API: version 0.11 or later is required\"\n";
  out << "#endif\n\n";
  for (const auto& e : spec.enums) {
    out << "enum class " << EnumCppTypeName(e.name) << " {\n";
    for (size_t i = 0; i < e.values.size(); i++) {
      const auto& v = e.values[i];
      out << "    " << EnumValueName(v.name) << " = " << v.value;
      out << (i + 1 == e.values.size() ? "\n" : ",\n");
    }
    out << "};\n\n";
  }
  out << "class " << spec.name << "_t : public kaitai::kstruct {\n\n";
  out << "public:\n\n";
  out << "    " << spec.name << "_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent = nullptr, " << spec.name << "_t* p__root = nullptr);\n\n";
  out << "private:\n";
  out << "    void _read();\n";
  out << "    void _clean_up();\n\n";
  out << "public:\n";
  out << "    ~" << spec.name << "_t();\n";
  for (const auto& inst : spec.instances) out << "    " << CppExprType(instance_types.at(inst.id)) << " " << inst.id << "();\n";
  for (const auto& attr : spec.attrs) out << "    " << CppAccessorType(attr, user_types) << " " << attr.id << "() const { return m_" << attr.id << "; }\n";
  out << "    " << spec.name << "_t* _root() const { return m__root; }\n";
  out << "    kaitai::kstruct* _parent() const { return m__parent; }\n\n";
  out << "private:\n";
  for (const auto& inst : spec.instances) {
    out << "    bool f_" << inst.id << ";\n";
    out << "    " << CppExprType(instance_types.at(inst.id)) << " m_" << inst.id << ";\n";
  }
  for (const auto& attr : spec.attrs) out << "    " << CppStorageType(attr, user_types) << " m_" << attr.id << ";\n";
  out << "    " << spec.name << "_t* m__root;\n";
  out << "    kaitai::kstruct* m__parent;\n";
  out << "};\n";
  return out.str();
}


std::string ValidationValueExpr(const std::string& target, const std::set<std::string>& attrs, const std::set<std::string>& instances) {
  if (attrs.find(target) != attrs.end() || instances.find(target) != instances.end()) return target + "()";
  return target;
}

std::string ValidationValueType(const std::string& target, const ir::Spec& spec,
                                const std::map<std::string, ExprType>& instance_types,
                                const std::map<std::string, ir::TypeRef>& user_types) {
  for (const auto& attr : spec.attrs) {
    if (attr.id == target) return CppStorageType(attr, user_types);
  }
  auto it = instance_types.find(target);
  if (it != instance_types.end()) return CppExprType(it->second);
  return "int32_t";
}

std::string RenderSource(const ir::Spec& spec) {
  const auto instance_types = ComputeInstanceTypes(spec);
  const auto user_types = BuildUserTypeMap(spec);
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
  for (const auto& attr : spec.attrs) {
    if (attr.if_expr.has_value()) {
      const std::string cond = RenderExpr(*attr.if_expr, attr_names, {}, -1);
      out << "    if (" << cond << ") {\n";
    }
    const std::string indent = attr.if_expr.has_value() ? "        " : "    ";
    const std::string nested_indent = attr.if_expr.has_value() ? "            " : "        ";
    if (attr.repeat == ir::Attr::RepeatKind::kNone) {
      if (attr.switch_on.has_value()) {
        out << indent << "m_" << attr.id << " = " << ReadSwitchExpr(attr, spec.default_endian, attr_names, {}, user_types) << ";\n";
      } else {
        out << indent << "m_" << attr.id << " = " << ReadExpr(attr, spec.default_endian, user_types) << ";\n";
      }
    } else if (attr.repeat == ir::Attr::RepeatKind::kEos) {
      out << indent << "while (!m__io->is_eof()) {\n";
      if (attr.switch_on.has_value()) out << nested_indent << "m_" << attr.id << ".push_back(" << ReadSwitchExpr(attr, spec.default_endian, attr_names, {}, user_types) << ");\n";
      else out << nested_indent << "m_" << attr.id << ".push_back(" << ReadExpr(attr, spec.default_endian, user_types) << ");\n";
      out << indent << "}\n";
    } else if (attr.repeat == ir::Attr::RepeatKind::kExpr) {
      out << indent << "for (int i = 0; i < " << RenderExpr(*attr.repeat_expr, attr_names, {}, -1) << "; i++) {\n";
      if (attr.switch_on.has_value()) out << nested_indent << "m_" << attr.id << ".push_back(" << ReadSwitchExpr(attr, spec.default_endian, attr_names, {}, user_types) << ");\n";
      else out << nested_indent << "m_" << attr.id << ".push_back(" << ReadExpr(attr, spec.default_endian, user_types) << ");\n";
      out << indent << "}\n";
    } else {
      out << indent << "do {\n";
      if (attr.switch_on.has_value()) {
        out << nested_indent << "auto repeat_item = " << ReadSwitchExpr(attr, spec.default_endian, attr_names, {}, user_types) << ";\n";
      } else {
        out << nested_indent << "auto repeat_item = " << ReadExpr(attr, spec.default_endian, user_types) << ";\n";
      }
      out << nested_indent << "m_" << attr.id << ".push_back(repeat_item);\n";
      out << indent << "} while (!(" << RenderExpr(*attr.repeat_expr, attr_names, {}, -1, "repeat_item") << "));\n";
    }
    if (attr.if_expr.has_value()) out << "    }\n";
  }
  std::set<std::string> all_instance_names;
  for (const auto& inst : spec.instances) all_instance_names.insert(inst.id);
  for (const auto& validation : spec.validations) {
    const std::string cond = RenderExpr(validation.condition_expr, attr_names, all_instance_names, -1);
    const std::string val_expr = ValidationValueExpr(validation.target, attr_names, all_instance_names);
    const std::string val_type = ValidationValueType(validation.target, spec, instance_types, user_types);
    out << "    if (!(" << cond << ")) {\n";
    out << "        throw kaitai::validation_expr_error<" << val_type << ">(" << val_expr
        << ", m__io, \"/valid/" << validation.target << "\");\n";
    out << "    }\n";
  }
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
bool WriteFile(const std::filesystem::path& path, const std::string& content, std::string* error) {
  std::ofstream out(path);
  if (!out) {
    *error = "failed to open output file: " + path.string();
    return false;
  }
  out << content;
  return true;
}

std::string RenderScriptingModule(const ir::Spec& spec, const std::string& target) {
  std::ostringstream out;
  out << "# This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild\n";
  out << "# target: " << target << "\n";
  out << "# id: " << spec.name << "\n";
  if (target == "python") {
    out << "class " << spec.name << ":\n";
    out << "    pass\n";
  } else if (target == "ruby") {
    out << "class " << spec.name << "\n";
    out << "end\n";
  } else {
    out << "local " << spec.name << " = {}\n";
    out << "return " << spec.name << "\n";
  }
  return out.str();
}

std::filesystem::path PythonOutputPath(const ir::Spec& spec, const CliOptions& options) {
  std::filesystem::path out_dir(options.out_dir);
  if (options.runtime.python_package.empty()) {
    return out_dir / (spec.name + ".py");
  }
  std::string package = options.runtime.python_package;
  for (char& c : package) if (c == '.') c = '/';
  return out_dir / package / (spec.name + ".py");
}

Result EmitScriptTarget(const ir::Spec& spec, const CliOptions& options, const std::string& target,
                        const std::filesystem::path& output_file) {
  const auto validate_subset = ValidateSupportedSubset(spec);
  if (!validate_subset.ok) return validate_subset;

  std::error_code ec;
  std::filesystem::create_directories(output_file.parent_path(), ec);
  if (ec) return {false, "failed to create output directory: " + ec.message()};

  std::string error;
  if (!WriteFile(output_file, RenderScriptingModule(spec, target), &error)) {
    return {false, error};
  }
  return {true, ""};
}


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

Result EmitLuaFromIr(const ir::Spec& spec, const CliOptions& options) {
  return EmitScriptTarget(spec, options, "lua", std::filesystem::path(options.out_dir) / (spec.name + ".lua"));
}

Result EmitWiresharkLuaFromIr(const ir::Spec& spec, const CliOptions& options) {
  return EmitScriptTarget(spec, options, "wireshark_lua",
                          std::filesystem::path(options.out_dir) / (spec.name + ".lua"));
}

Result EmitPythonFromIr(const ir::Spec& spec, const CliOptions& options) {
  return EmitScriptTarget(spec, options, "python", PythonOutputPath(spec, options));
}

Result EmitRubyFromIr(const ir::Spec& spec, const CliOptions& options) {
  return EmitScriptTarget(spec, options, "ruby", std::filesystem::path(options.out_dir) / (spec.name + ".rb"));
}

} // namespace kscpp::codegen
