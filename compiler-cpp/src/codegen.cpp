#include "codegen.h"

#include <cctype>
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
  if (op == "and")
    return "&&";
  if (op == "or")
    return "||";
  if (op == "xor")
    return "^";
  if (op == "not")
    return "!";
  return op;
}

std::string ToUpperCamelIdentifier(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  bool uppercase_next = true;
  for (char c : value) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) == 0) {
      uppercase_next = true;
      continue;
    }
    if (uppercase_next) {
      out.push_back(static_cast<char>(std::toupper(uc)));
      uppercase_next = false;
    } else {
      out.push_back(c);
    }
  }
  if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])) != 0) {
    out.insert(out.begin(), '_');
  }
  return out;
}

int ExprPrecedence(const ir::Expr& expr) {
  if (expr.kind != ir::Expr::Kind::kBinary) {
    return 100;
  }
  const std::string op = NormalizeOp(expr.text);
  if (op == "||") return 10;
  if (op == "&&") return 20;
  if (op == "|") return 30;
  if (op == "^") return 35;
  if (op == "&") return 40;
  if (op == "==" || op == "!=") return 45;
  if (op == "<" || op == "<=" || op == ">" || op == ">=") return 50;
  if (op == "<<" || op == ">>") return 55;
  if (op == "+" || op == "-") return 60;
  if (op == "*" || op == "/" || op == "%") return 70;
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
    if (inst.kind != ir::Instance::Kind::kValue) continue;
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
    const bool unresolved_user_type =
        !resolved_type.has_value() && attr.type.kind == ir::TypeRef::Kind::kUser;
    if (!resolved_type.has_value() && !unresolved_user_type) {
      return {false, "not yet supported: attr type must resolve to primitive type"};
    }
    if (unresolved_user_type) {
      if (attr.encoding.has_value() || attr.process.has_value() || attr.enum_name.has_value()) {
        return {false, "not yet supported: complex user-type attrs in this migration slice"};
      }
      continue;
    }
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
    const bool unresolved_user_type =
        !resolved_type.has_value() && attr.type.kind == ir::TypeRef::Kind::kUser;
    if (!resolved_type.has_value() && !unresolved_user_type) {
      return {false, "not yet supported: attr type must resolve to primitive type"};
    }
    if (unresolved_user_type) {
      continue;
    }
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
      if (expr.text != "-" && expr.text != "!" && expr.text != "not" && expr.text != "~") return {false, "not yet supported: unary operator \"" + expr.text + "\""};
      return validate_expr(*expr.lhs);
    case ir::Expr::Kind::kBinary: {
      static const std::set<std::string> supported_ops = {
          "+", "-", "*", "/", "%", "==", "!=", ">", ">=", "<", "<=", "&&", "||", "and", "or", "&", "|", "^", "xor", "<<", ">>"};
      if (supported_ops.find(expr.text) == supported_ops.end()) return {false, "not yet supported: binary operator \"" + expr.text + "\""};
      auto lhs = validate_expr(*expr.lhs); if (!lhs.ok) return lhs; return validate_expr(*expr.rhs);
    }
    }
    return {false, "not yet supported: unknown expression kind"};
  };

  for (const auto& inst : spec.instances) {
    if (inst.kind == ir::Instance::Kind::kValue) {
      auto result = validate_expr(inst.value_expr);
      if (!result.ok) return result;
    } else {
      auto resolved_type = resolve_primitive(inst.type);
      if (!resolved_type.has_value()) {
        return {false, "not yet supported: parse instance type must resolve to primitive type"};
      }
      if (inst.pos_expr.has_value()) {
        auto result = validate_expr(*inst.pos_expr);
        if (!result.ok) return result;
      }
      if (inst.size_expr.has_value()) {
        auto result = validate_expr(*inst.size_expr);
        if (!result.ok) return result;
      }
    }
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

std::optional<ir::PrimitiveType> EffectiveAttrPrimitive(const ir::Attr& attr,
                                                        const std::map<std::string, ir::TypeRef>& user_types) {
  if (attr.switch_on.has_value() && !attr.switch_cases.empty()) {
    return ResolvePrimitiveType(attr.switch_cases.front().type, user_types);
  }
  return ResolvePrimitiveType(attr.type, user_types);
}

std::string CppAttrType(const ir::Attr& attr, const std::map<std::string, ir::TypeRef>& user_types) {
  if (attr.enum_name.has_value()) return EnumCppTypeName(*attr.enum_name);
  const auto primitive = EffectiveAttrPrimitive(attr, user_types);
  return CppFieldType(primitive.value_or(ir::PrimitiveType::kU1));
}

std::string CppReadPrimitiveExpr(ir::PrimitiveType primitive, std::optional<ir::Endian> override_endian,
                                 ir::Endian default_endian) {
  if (primitive == ir::PrimitiveType::kBytes) return "m__io->read_bytes_full()";
  if (primitive == ir::PrimitiveType::kStr) return "std::string()";
  return "m__io->" + ReadMethod(primitive, override_endian.value_or(default_endian)) + "()";
}

std::string ReadExpr(const ir::Attr& attr, ir::Endian default_endian,
                     const std::set<std::string>& attrs, const std::set<std::string>& instances,
                     const std::map<std::string, ir::TypeRef>& user_types) {
  const auto primitive = ResolvePrimitiveType(attr.type, user_types).value_or(ir::PrimitiveType::kU1);
  if (primitive == ir::PrimitiveType::kBytes) {
    std::string read = attr.size_expr.has_value() ?
      ("m__io->read_bytes(" + RenderExpr(*attr.size_expr, attrs, instances, -1) + ")") :
      "m__io->read_bytes_full()";
    if (attr.process.has_value() && attr.process->kind == ir::Attr::Process::Kind::kXorConst) {
      read = "kaitai::kstream::process_xor_one(" + read + ", " + std::to_string(attr.process->xor_const) + ")";
    }
    return read;
  }
  if (primitive == ir::PrimitiveType::kStr) {
    if (!attr.size_expr.has_value()) return "std::string()";
    const std::string enc = attr.encoding.value_or("UTF-8");
    return "kaitai::kstream::bytes_to_str(m__io->read_bytes(" + RenderExpr(*attr.size_expr, attrs, instances, -1) + "), \"" + enc + "\")";
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

bool CanRenderNativeSwitch(const ir::Attr& attr) {
  if (!attr.switch_on.has_value()) return false;
  for (const auto& c : attr.switch_cases) {
    if (!c.match_expr.has_value()) continue;
    if (c.match_expr->kind != ir::Expr::Kind::kInt) return false;
  }
  return true;
}

std::string CppStorageType(const ir::Attr& attr, const std::map<std::string, ir::TypeRef>& user_types) {
  std::string base = attr.switch_on.has_value() ? SwitchCaseType(attr, user_types) : CppAttrType(attr, user_types);
  if (attr.repeat != ir::Attr::RepeatKind::kNone) return "std::unique_ptr<std::vector<" + base + ">>";
  return base;
}

std::string CppAccessorType(const ir::Attr& attr, const std::map<std::string, ir::TypeRef>& user_types) {
  if (attr.repeat != ir::Attr::RepeatKind::kNone) {
    const std::string base = attr.switch_on.has_value() ? SwitchCaseType(attr, user_types) : CppAttrType(attr, user_types);
    return "std::vector<" + base + ">*";
  }
  return CppStorageType(attr, user_types);
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

std::string CppUserTypeName(const std::string& type_name) {
  if (type_name.empty()) return "kaitai::kstruct";
  std::vector<std::string> parts;
  size_t start = 0;
  while (start < type_name.size()) {
    const size_t pos = type_name.find("::", start);
    if (pos == std::string::npos) {
      parts.push_back(type_name.substr(start));
      break;
    }
    parts.push_back(type_name.substr(start, pos - start));
    start = pos + 2;
  }
  if (parts.empty()) return "kaitai::kstruct";
  std::ostringstream out;
  if (parts.size() == 1) {
    out << parts[0] << "_t";
    return out.str();
  }
  out << parts[0] << "_t";
  for (size_t i = 1; i < parts.size(); i++) {
    out << "::" << parts[i] << "_t";
  }
  return out.str();
}

std::string CppTypeForTypeRef(const ir::TypeRef& type_ref,
                              const std::map<std::string, ir::TypeRef>& user_types) {
  const auto primitive = ResolvePrimitiveType(type_ref, user_types);
  if (primitive.has_value()) return CppFieldType(*primitive);
  if (type_ref.kind == ir::TypeRef::Kind::kUser) return CppUserTypeName(type_ref.user_type) + "*";
  return "uint8_t";
}

std::string CppInstanceType(const ir::Instance& inst,
                            const std::map<std::string, ExprType>& instance_types,
                            const std::map<std::string, ir::TypeRef>& user_types) {
  if (inst.kind == ir::Instance::Kind::kParse) {
    return CppTypeForTypeRef(inst.type, user_types);
  }
  auto it = instance_types.find(inst.id);
  if (it == instance_types.end()) return "int32_t";
  return CppExprType(it->second);
}

std::string CppReadParseInstanceExpr(const ir::Instance& inst, ir::Endian default_endian,
                                     const std::set<std::string>& attrs,
                                     const std::set<std::string>& instances,
                                     const std::map<std::string, ir::TypeRef>& user_types) {
  const auto primitive = ResolvePrimitiveType(inst.type, user_types).value_or(ir::PrimitiveType::kU1);
  if (primitive == ir::PrimitiveType::kBytes) {
    if (inst.size_expr.has_value()) {
      return "m__io->read_bytes(" + RenderExpr(*inst.size_expr, attrs, instances, -1) + ")";
    }
    return "m__io->read_bytes_full()";
  }
  if (primitive == ir::PrimitiveType::kStr) {
    if (!inst.size_expr.has_value()) return "std::string()";
    const std::string enc = inst.encoding.value_or("UTF-8");
    return "kaitai::kstream::bytes_to_str(m__io->read_bytes(" +
           RenderExpr(*inst.size_expr, attrs, instances, -1) + "), \"" + enc + "\")";
  }
  return "m__io->" + ReadMethod(primitive, inst.endian_override.value_or(default_endian)) + "()";
}

bool NeedsVectorInclude(const ir::Spec& spec) {
  for (const auto& attr : spec.attrs) {
    if (attr.repeat != ir::Attr::RepeatKind::kNone) return true;
  }
  return false;
}

bool NeedsStringInclude(const ir::Spec& spec, const std::map<std::string, ir::TypeRef>& user_types) {
  (void)spec;
  (void)user_types;
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
  std::vector<std::string> raw_accessors;
  std::vector<std::string> raw_fields;
  for (const auto& inst : spec.instances) {
    out << "    " << CppInstanceType(inst, instance_types, user_types) << " " << inst.id << "();\n";
  }
  for (const auto& attr : spec.attrs) {
    if (attr.repeat != ir::Attr::RepeatKind::kNone) {
      out << "    " << CppAccessorType(attr, user_types) << " " << attr.id << "() const { return m_" << attr.id << ".get(); }\n";
    } else {
      out << "    " << CppAccessorType(attr, user_types) << " " << attr.id << "() const { return m_" << attr.id << "; }\n";
    }
    const auto primitive = ResolvePrimitiveType(attr.type, user_types).value_or(ir::PrimitiveType::kU1);
    if (primitive == ir::PrimitiveType::kBytes &&
        attr.process.has_value() &&
        attr.process->kind == ir::Attr::Process::Kind::kXorConst &&
        attr.repeat == ir::Attr::RepeatKind::kNone) {
      raw_accessors.push_back("    std::string _raw_" + attr.id + "() const { return m__raw_" + attr.id + "; }\n");
      raw_fields.push_back("    std::string m__raw_" + attr.id + ";\n");
    }
  }
  out << "    " << spec.name << "_t* _root() const { return m__root; }\n";
  out << "    kaitai::kstruct* _parent() const { return m__parent; }\n";
  for (const auto& acc : raw_accessors) out << acc;
  out << "\n";
  out << "private:\n";
  for (const auto& inst : spec.instances) {
    out << "    bool f_" << inst.id << ";\n";
    out << "    " << CppInstanceType(inst, instance_types, user_types) << " m_" << inst.id << ";\n";
  }
  for (const auto& attr : spec.attrs) {
    out << "    " << CppStorageType(attr, user_types) << " m_" << attr.id << ";\n";
  }
  out << "    " << spec.name << "_t* m__root;\n";
  out << "    kaitai::kstruct* m__parent;\n";
  for (const auto& field : raw_fields) out << field;
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
  for (const auto& inst : spec.instances) {
    if (inst.id == target) return CppInstanceType(inst, instance_types, user_types);
  }
  return "int32_t";
}

std::string RenderSource(const ir::Spec& spec) {
  const auto instance_types = ComputeInstanceTypes(spec);
  const auto user_types = BuildUserTypeMap(spec);
  std::set<std::string> attr_names;
  for (const auto& attr : spec.attrs) attr_names.insert(attr.id);

  std::ostringstream out;
  out << "// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild\n\n";
  out << "#include \"" << spec.name << ".h\"\n";
  if (!spec.validations.empty()) {
    out << "#include \"kaitai/exceptions.h\"\n";
  }
  out << "\n";
  out << spec.name << "_t::" << spec.name << "_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent, " << spec.name << "_t* p__root) : kaitai::kstruct(p__io) {\n";
  out << "    m__parent = p__parent;\n";
  out << "    m__root = p__root ? p__root : this;\n";
  for (const auto& inst : spec.instances) out << "    f_" << inst.id << " = false;\n";
  for (const auto& attr : spec.attrs) {
    if (attr.repeat != ir::Attr::RepeatKind::kNone) {
      out << "    m_" << attr.id << " = nullptr;\n";
    }
  }
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
        if (CanRenderNativeSwitch(attr)) {
          out << indent << "switch (" << RenderExpr(*attr.switch_on, attr_names, {}, -1) << ") {\n";
          bool has_else = false;
          for (const auto& c : attr.switch_cases) {
            const auto case_primitive = ResolvePrimitiveType(c.type, user_types).value_or(ir::PrimitiveType::kU1);
            if (!c.match_expr.has_value()) {
              out << indent << "default: {\n";
              has_else = true;
            } else {
              out << indent << "case " << c.match_expr->int_value << ": {\n";
            }
            out << nested_indent << "m_" << attr.id << " = "
                << CppReadPrimitiveExpr(case_primitive, attr.endian_override, spec.default_endian) << ";\n";
            out << nested_indent << "break;\n";
            out << indent << "}\n";
          }
          if (!has_else) {
            out << indent << "default: {\n";
            out << nested_indent << "throw std::runtime_error(\"switch-on has no matching case\");\n";
            out << indent << "}\n";
          }
          out << indent << "}\n";
        } else {
          out << indent << "m_" << attr.id << " = " << ReadSwitchExpr(attr, spec.default_endian, attr_names, {}, user_types) << ";\n";
        }
      } else {
        const auto primitive = ResolvePrimitiveType(attr.type, user_types).value_or(ir::PrimitiveType::kU1);
        if (primitive == ir::PrimitiveType::kBytes &&
            attr.process.has_value() &&
            attr.process->kind == ir::Attr::Process::Kind::kXorConst) {
          const std::string raw_read = attr.size_expr.has_value() ?
            ("m__io->read_bytes(" + RenderExpr(*attr.size_expr, attr_names, {}, -1) + ")") :
            "m__io->read_bytes_full()";
          out << indent << "m__raw_" << attr.id << " = " << raw_read << ";\n";
          out << indent << "m_" << attr.id << " = kaitai::kstream::process_xor_one(m__raw_" << attr.id
              << ", " << attr.process->xor_const << ");\n";
        } else {
          out << indent << "m_" << attr.id << " = " << ReadExpr(attr, spec.default_endian, attr_names, {}, user_types) << ";\n";
        }
      }
    } else if (attr.repeat == ir::Attr::RepeatKind::kEos) {
      out << indent << "m_" << attr.id << " = std::unique_ptr<std::vector<"
          << (attr.switch_on.has_value() ? SwitchCaseType(attr, user_types) : CppAttrType(attr, user_types))
          << ">>(new std::vector<"
          << (attr.switch_on.has_value() ? SwitchCaseType(attr, user_types) : CppAttrType(attr, user_types))
          << ">());\n";
      out << indent << "while (!m__io->is_eof()) {\n";
      if (attr.switch_on.has_value()) out << nested_indent << "m_" << attr.id << "->push_back(" << ReadSwitchExpr(attr, spec.default_endian, attr_names, {}, user_types) << ");\n";
      else out << nested_indent << "m_" << attr.id << "->push_back(" << ReadExpr(attr, spec.default_endian, attr_names, {}, user_types) << ");\n";
      out << indent << "}\n";
    } else if (attr.repeat == ir::Attr::RepeatKind::kExpr) {
      out << indent << "m_" << attr.id << " = std::unique_ptr<std::vector<"
          << (attr.switch_on.has_value() ? SwitchCaseType(attr, user_types) : CppAttrType(attr, user_types))
          << ">>(new std::vector<"
          << (attr.switch_on.has_value() ? SwitchCaseType(attr, user_types) : CppAttrType(attr, user_types))
          << ">());\n";
      out << indent << "const int l_" << attr.id << " = " << RenderExpr(*attr.repeat_expr, attr_names, {}, -1) << ";\n";
      out << indent << "for (int i = 0; i < l_" << attr.id << "; i++) {\n";
      if (attr.switch_on.has_value()) out << nested_indent << "m_" << attr.id << "->push_back(std::move(" << ReadSwitchExpr(attr, spec.default_endian, attr_names, {}, user_types) << "));\n";
      else out << nested_indent << "m_" << attr.id << "->push_back(std::move(" << ReadExpr(attr, spec.default_endian, attr_names, {}, user_types) << "));\n";
      out << indent << "}\n";
    } else {
      out << indent << "m_" << attr.id << " = std::unique_ptr<std::vector<"
          << (attr.switch_on.has_value() ? SwitchCaseType(attr, user_types) : CppAttrType(attr, user_types))
          << ">>(new std::vector<"
          << (attr.switch_on.has_value() ? SwitchCaseType(attr, user_types) : CppAttrType(attr, user_types))
          << ">());\n";
      out << indent << "do {\n";
      if (attr.switch_on.has_value()) {
        out << nested_indent << "auto repeat_item = " << ReadSwitchExpr(attr, spec.default_endian, attr_names, {}, user_types) << ";\n";
      } else {
        out << nested_indent << "auto repeat_item = " << ReadExpr(attr, spec.default_endian, attr_names, {}, user_types) << ";\n";
      }
      out << nested_indent << "m_" << attr.id << "->push_back(repeat_item);\n";
      out << indent << "} while (!(" << RenderExpr(*attr.repeat_expr, attr_names, {}, -1, "repeat_item") << "));\n";
    }
    if (attr.if_expr.has_value()) out << "    }\n";
  }
  std::set<std::string> all_instance_names;
  for (const auto& inst : spec.instances) all_instance_names.insert(inst.id);
  std::map<std::string, size_t> attr_index_by_id;
  for (size_t i = 0; i < spec.attrs.size(); i++) {
    attr_index_by_id[spec.attrs[i].id] = i;
  }
  for (const auto& validation : spec.validations) {
    const auto& cond_expr = validation.condition_expr;
    bool emitted_specialized = false;
    if (cond_expr.kind == ir::Expr::Kind::kBinary && cond_expr.text == "==" &&
        attr_index_by_id.find(validation.target) != attr_index_by_id.end()) {
      const auto is_target_name = [&](const ir::Expr& e) {
        return e.kind == ir::Expr::Kind::kName && e.text == validation.target;
      };
      const auto is_int_lit = [&](const ir::Expr& e) { return e.kind == ir::Expr::Kind::kInt; };
      const bool lhs_target_rhs_int = is_target_name(*cond_expr.lhs) && is_int_lit(*cond_expr.rhs);
      const bool rhs_target_lhs_int = is_target_name(*cond_expr.rhs) && is_int_lit(*cond_expr.lhs);
      if (lhs_target_rhs_int || rhs_target_lhs_int) {
        const long long expected = lhs_target_rhs_int ? cond_expr.rhs->int_value : cond_expr.lhs->int_value;
        const auto attr_index = attr_index_by_id[validation.target];
        const auto val_type = ValidationValueType(validation.target, spec, instance_types, user_types);
        out << "    if (!(m_" << validation.target << " == " << expected << ")) {\n";
        out << "        throw kaitai::validation_not_equal_error<" << val_type << ">(" << expected
            << ", m_" << validation.target << ", m__io, std::string(\"/seq/" << attr_index << "\"));\n";
        out << "    }\n";
        emitted_specialized = true;
      }
    }
    if (!emitted_specialized) {
      const std::string cond = RenderExpr(validation.condition_expr, attr_names, all_instance_names, -1);
      const std::string val_expr = ValidationValueExpr(validation.target, attr_names, all_instance_names);
      const std::string val_type = ValidationValueType(validation.target, spec, instance_types, user_types);
      out << "    if (!(" << cond << ")) {\n";
      out << "        throw kaitai::validation_expr_error<" << val_type << ">(" << val_expr
          << ", m__io, \"/valid/" << validation.target << "\");\n";
      out << "    }\n";
    }
  }
  out << "}\n\n";

  out << spec.name << "_t::~" << spec.name << "_t() {\n";
  out << "    _clean_up();\n";
  out << "}\n\n";

  out << "void " << spec.name << "_t::_clean_up() {\n";
  for (const auto& inst : spec.instances) {
    out << "    if (f_" << inst.id << ") {\n";
    out << "    }\n";
  }
  out << "}\n";

  std::set<std::string> known_instances;
  for (const auto& inst : spec.instances) {
    out << "\n";
    out << CppInstanceType(inst, instance_types, user_types) << " " << spec.name << "_t::" << inst.id << "() {\n";
    out << "    if (f_" << inst.id << ")\n";
    out << "        return m_" << inst.id << ";\n";
    out << "    f_" << inst.id << " = true;\n";
    if (inst.kind == ir::Instance::Kind::kParse) {
      out << "    std::streampos _pos = m__io->pos();\n";
      if (inst.pos_expr.has_value()) {
        out << "    m__io->seek(" << RenderExpr(*inst.pos_expr, attr_names, known_instances, -1) << ");\n";
      }
      out << "    m_" << inst.id << " = " << CppReadParseInstanceExpr(inst, spec.default_endian, attr_names, known_instances, user_types) << ";\n";
      out << "    m__io->seek(_pos);\n";
    } else {
      std::string rendered = RenderExpr(inst.value_expr, attr_names, known_instances, -1);
      auto it = instance_types.find(inst.id);
      if (it != instance_types.end() && it->second == ExprType::kBool) rendered = " " + rendered + " ";
      out << "    m_" << inst.id << " = " << rendered << ";\n";
    }
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


std::string RenderPythonModule(const ir::Spec& spec) {
  const std::string class_name = ToUpperCamelIdentifier(spec.name);
  std::set<std::string> attrs;
  for (const auto& a : spec.attrs) attrs.insert(a.id);
  std::set<std::string> known_instances;
  const auto user_types = BuildUserTypeMap(spec);

  std::function<std::string(const ir::Expr&, int)> expr;
  expr = [&](const ir::Expr& e, int parent_prec) {
    switch (e.kind) {
    case ir::Expr::Kind::kInt: return std::to_string(e.int_value);
    case ir::Expr::Kind::kBool: return e.bool_value ? std::string("True") : std::string("False");
    case ir::Expr::Kind::kName:
      if (attrs.find(e.text) != attrs.end() || known_instances.find(e.text) != known_instances.end()) return "self." + e.text;
      return e.text;
    case ir::Expr::Kind::kUnary: return "(" + NormalizeOp(e.text) + expr(*e.lhs, 90) + ")";
    case ir::Expr::Kind::kBinary: {
      std::string op = NormalizeOp(e.text);
      const int prec = ExprPrecedence(e);
      std::string rendered = expr(*e.lhs, prec) + " " + op + " " + expr(*e.rhs, prec + 1);
      if (prec <= parent_prec) rendered = "(" + rendered + ")";
      return rendered;
    }
    }
    return std::string("0");
  };

  auto read_primitive = [&](ir::PrimitiveType primitive, std::optional<ir::Endian> override_endian) {
    if (primitive == ir::PrimitiveType::kBytes) return std::string("self._io.read_bytes_full()");
    if (primitive == ir::PrimitiveType::kStr) return std::string("''");
    return std::string("self._io.") + ReadMethod(primitive, override_endian.value_or(spec.default_endian)) + "()";
  };

  auto read_value = [&](const ir::Attr& attr) {
    const auto primitive = ResolvePrimitiveType(attr.type, user_types).value_or(ir::PrimitiveType::kU1);
    if (primitive == ir::PrimitiveType::kBytes) {
      std::string read = attr.size_expr.has_value() ? ("self._io.read_bytes(" + expr(*attr.size_expr, -1) + ")") : "self._io.read_bytes_full()";
      if (attr.process.has_value() && attr.process->kind == ir::Attr::Process::Kind::kXorConst) {
        read = "KaitaiStream.process_xor_one(" + read + ", " + std::to_string(attr.process->xor_const) + ")";
      }
      return read;
    }
    if (primitive == ir::PrimitiveType::kStr) {
      return attr.size_expr.has_value() ? ("KaitaiStream.bytes_to_str(self._io.read_bytes(" + expr(*attr.size_expr, -1) + "), '" + attr.encoding.value_or("UTF-8") + "')") : "''";
    }
    return read_primitive(primitive, attr.endian_override);
  };

  auto read_attr = [&](const ir::Attr& attr) {
    if (attr.switch_on.has_value()) {
      const auto primitive = ResolvePrimitiveType(attr.type, user_types).value_or(ir::PrimitiveType::kU1);
      std::ostringstream sw;
      sw << "_on = " << expr(*attr.switch_on, -1) << "\n";
      bool wrote_else = false;
      for (size_t i = 0; i < attr.switch_cases.size(); i++) {
        const auto& c = attr.switch_cases[i];
        if (!c.match_expr.has_value()) {
          sw << "else:\n";
          wrote_else = true;
        } else {
          sw << (i == 0 ? "if" : "elif") << " _on == " << expr(*c.match_expr, -1) << ":\n";
        }
        const auto c_prim = ResolvePrimitiveType(c.type, user_types).value_or(primitive);
        sw << "    self." << attr.id << " = " << read_primitive(c_prim, attr.endian_override) << "\n";
      }
      if (!wrote_else) sw << "else:\n    self." << attr.id << " = " << read_primitive(primitive, attr.endian_override) << "\n";
      return sw.str();
    }
    if (ResolvePrimitiveType(attr.type, user_types).value_or(ir::PrimitiveType::kU1) == ir::PrimitiveType::kBytes &&
        attr.process.has_value() &&
        attr.process->kind == ir::Attr::Process::Kind::kXorConst) {
      std::string raw = attr.size_expr.has_value() ? ("self._io.read_bytes(" + expr(*attr.size_expr, -1) + ")") : "self._io.read_bytes_full()";
      std::ostringstream lines;
      lines << "self._raw_" << attr.id << " = " << raw << "\n";
      lines << "self." << attr.id << " = KaitaiStream.process_xor_one(self._raw_" << attr.id << ", " << attr.process->xor_const << ")\n";
      return lines.str();
    }
    std::string read = read_value(attr);
    return std::string("self.") + attr.id + " = " + read + "\n";
  };

  auto read_parse_instance = [&](const ir::Instance& inst) {
    const auto primitive = ResolvePrimitiveType(inst.type, user_types).value_or(ir::PrimitiveType::kU1);
    if (primitive == ir::PrimitiveType::kBytes) {
      if (inst.size_expr.has_value()) return "self._io.read_bytes(" + expr(*inst.size_expr, -1) + ")";
      return std::string("self._io.read_bytes_full()");
    }
    if (primitive == ir::PrimitiveType::kStr) {
      if (!inst.size_expr.has_value()) return std::string("''");
      return "(self._io.read_bytes(" + expr(*inst.size_expr, -1) + ")).decode(u\"" +
             inst.encoding.value_or("UTF-8") + "\")";
    }
    return std::string("self._io.") + ReadMethod(primitive, inst.endian_override.value_or(spec.default_endian)) + "()";
  };

  std::ostringstream out;
  out << "# This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild\n";
  out << "# type: ignore\n\n";
  out << "import kaitaistruct\n";
  out << "from kaitaistruct import KaitaiStruct, KaitaiStream, BytesIO";
  if (!spec.validations.empty()) out << ", ValidationExprError";
  out << "\n\n";
  out << "if getattr(kaitaistruct, 'API_VERSION', (0, 9)) < (0, 11):\n";
  out << "    raise Exception(\"Incompatible Kaitai Struct Python API: 0.11 or later is required, but you have %s\" % (kaitaistruct.__version__))\n\n";
  out << "class " << class_name << "(KaitaiStruct):\n";
  out << "    def __init__(self, _io, _parent=None, _root=None):\n";
  out << "        super(" << class_name << ", self).__init__(_io)\n";
  out << "        self._parent = _parent\n";
  out << "        self._root = _root or self\n";
  out << "        self._read()\n\n";
  out << "    def _read(self):\n";
  if (spec.attrs.empty() && spec.validations.empty()) out << "        pass\n";
  for (const auto& attr : spec.attrs) {
    if (attr.repeat == ir::Attr::RepeatKind::kNone) {
      std::string read = read_attr(attr);
      std::istringstream in(read);
      std::string line;
      while (std::getline(in, line)) if (!line.empty()) out << "        " << line << "\n";
    } else if (attr.repeat == ir::Attr::RepeatKind::kEos) {
      out << "        self." << attr.id << " = []\n";
      out << "        while not self._io.is_eof():\n";
      out << "            _ = " << read_value(attr) << "\n";
      out << "            self." << attr.id << ".append(_)\n";
    } else if (attr.repeat == ir::Attr::RepeatKind::kExpr) {
      out << "        self." << attr.id << " = []\n";
      out << "        for i in range(" << expr(*attr.repeat_expr, -1) << "):\n";
      out << "            self." << attr.id << ".append(" << read_value(attr) << ")\n";
    } else {
      out << "        self." << attr.id << " = []\n";
      out << "        while True:\n";
      out << "            _ = " << read_value(attr) << "\n";
      out << "            self." << attr.id << ".append(_)\n";
      out << "            if " << expr(*attr.repeat_expr, -1) << ":\n";
      out << "                break\n";
    }
  }
  for (const auto& v : spec.validations) {
    out << "        if not (" << expr(v.condition_expr, -1) << "):\n";
    out << "            raise ValidationExprError(self." << v.target << ", self._io, '/valid/" << v.target << "')\n";
  }

  out << "\n";
  out << "    def _fetch_instances(self):\n";
  out << "        pass\n";
  for (const auto& attr : spec.attrs) {
    if (attr.repeat != ir::Attr::RepeatKind::kNone) {
      out << "        for i in range(len(self." << attr.id << ")):\n";
      out << "            pass\n";
    }
  }
  for (const auto& inst : spec.instances) {
    out << "        _ = self." << inst.id << "\n";
    out << "        if hasattr(self, '_m_" << inst.id << "'):\n";
    out << "            pass\n";
  }

  for (const auto& inst : spec.instances) {
    out << "\n    @property\n";
    out << "    def " << inst.id << "(self):\n";
    out << "        if hasattr(self, '_m_" << inst.id << "'):\n";
    out << "            return self._m_" << inst.id << "\n";
    if (inst.kind == ir::Instance::Kind::kParse) {
      out << "\n";
      out << "        _pos = self._io.pos()\n";
      if (inst.pos_expr.has_value()) {
        out << "        self._io.seek(" << expr(*inst.pos_expr, -1) << ")\n";
      }
      out << "        self._m_" << inst.id << " = " << read_parse_instance(inst) << "\n";
      out << "        self._io.seek(_pos)\n";
      out << "        return getattr(self, '_m_" << inst.id << "', None)\n";
    } else {
      out << "        self._m_" << inst.id << " = " << expr(inst.value_expr, -1) << "\n";
      out << "        return self._m_" << inst.id << "\n";
    }
    known_instances.insert(inst.id);
  }
  return out.str();
}

std::string RenderRubyModule(const ir::Spec& spec) {
  const std::string class_name = ToUpperCamelIdentifier(spec.name);
  std::set<std::string> attrs;
  for (const auto& a : spec.attrs) attrs.insert(a.id);
  std::set<std::string> known_instances;
  const auto user_types = BuildUserTypeMap(spec);
  std::function<std::string(const ir::Expr&, int)> expr;
  expr = [&](const ir::Expr& e, int parent_prec) {
    switch (e.kind) {
    case ir::Expr::Kind::kInt: return std::to_string(e.int_value);
    case ir::Expr::Kind::kBool: return e.bool_value ? std::string("true") : std::string("false");
    case ir::Expr::Kind::kName: return (attrs.find(e.text) != attrs.end() || known_instances.find(e.text) != known_instances.end()) ? ("@" + e.text) : e.text;
    case ir::Expr::Kind::kUnary: return "(" + NormalizeOp(e.text) + expr(*e.lhs, 90) + ")";
    case ir::Expr::Kind::kBinary: {
      const int prec = ExprPrecedence(e);
      std::string rendered = expr(*e.lhs, prec) + " " + NormalizeOp(e.text) + " " + expr(*e.rhs, prec + 1);
      if (prec <= parent_prec) rendered = "(" + rendered + ")";
      return rendered;
    }
    }
    return std::string("0");
  };

  auto read_primitive = [&](ir::PrimitiveType primitive, std::optional<ir::Endian> override_endian) {
    if (primitive == ir::PrimitiveType::kBytes) return std::string("@_io.read_bytes_full");
    if (primitive == ir::PrimitiveType::kStr) return std::string("''");
    return std::string("@_io.") + ReadMethod(primitive, override_endian.value_or(spec.default_endian));
  };

  auto read_parse_instance = [&](const ir::Instance& inst) {
    const auto primitive = ResolvePrimitiveType(inst.type, user_types).value_or(ir::PrimitiveType::kU1);
    if (primitive == ir::PrimitiveType::kBytes) {
      if (inst.size_expr.has_value()) return "@_io.read_bytes(" + expr(*inst.size_expr, -1) + ")";
      return std::string("@_io.read_bytes_full");
    }
    if (primitive == ir::PrimitiveType::kStr) {
      if (!inst.size_expr.has_value()) return std::string("\"\"");
      return "(@_io.read_bytes(" + expr(*inst.size_expr, -1) + ")).force_encoding(\"" +
             inst.encoding.value_or("UTF-8") + "\").encode('UTF-8')";
    }
    return "@_io." + ReadMethod(primitive, inst.endian_override.value_or(spec.default_endian));
  };

  std::ostringstream out;
  out << "# This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild\n";
  out << "\n";
  out << "require 'kaitai/struct/struct'\n\n";
  out << "unless Gem::Version.new(Kaitai::Struct::VERSION) >= Gem::Version.new('0.11')\n";
  out << "  raise \"Incompatible Kaitai Struct Ruby API: 0.11 or later is required, but you have #{Kaitai::Struct::VERSION}\"\n";
  out << "end\n\n";
  out << "class " << class_name << " < Kaitai::Struct::Struct\n";
  out << "  def initialize(_io, _parent = nil, _root = nil)\n";
  out << "    super(_io, _parent, _root || self)\n";
  out << "    _read\n";
  out << "  end\n\n";
  out << "  def _read\n";
  for (const auto& attr : spec.attrs) {
    const auto primitive = ResolvePrimitiveType(attr.type, user_types).value_or(ir::PrimitiveType::kU1);
    std::string read = read_primitive(primitive, attr.endian_override);
    if (primitive == ir::PrimitiveType::kBytes) {
      read = attr.size_expr.has_value() ? ("@_io.read_bytes(" + expr(*attr.size_expr, -1) + ")") : "@_io.read_bytes_full";
      if (attr.process.has_value() && attr.process->kind == ir::Attr::Process::Kind::kXorConst) {
        read = "Kaitai::Struct::Stream.process_xor_one(" + read + ", " + std::to_string(attr.process->xor_const) + ")";
      }
    }
    if (attr.repeat == ir::Attr::RepeatKind::kExpr) {
      out << "    @" << attr.id << " = []\n";
      out << "    (" << expr(*attr.repeat_expr, -1) << ").times { |i|\n";
      out << "      @" << attr.id << " << " << read << "\n";
      out << "    }\n";
    } else {
      out << "    @" << attr.id << " = " << read << "\n";
    }
  }
  for (const auto& v : spec.validations) {
    out << "    raise Kaitai::Struct::ValidationExprError.new(@" << v.target << ", @_io, '/valid/" << v.target << "') if !(" << expr(v.condition_expr, -1) << ")\n";
  }
  out << "    self\n";
  out << "  end\n";
  for (const auto& attr : spec.attrs) out << "  attr_reader :" << attr.id << "\n";
  bool first_instance = true;
  for (const auto& inst : spec.instances) {
    if (!first_instance) out << "\n";
    out << "  def " << inst.id << "\n";
    out << "    return @" << inst.id << " unless @" << inst.id << ".nil?\n";
    if (inst.kind == ir::Instance::Kind::kParse) {
      out << "    _pos = @_io.pos\n";
      if (inst.pos_expr.has_value()) {
        out << "    @_io.seek(" << expr(*inst.pos_expr, -1) << ")\n";
      }
      out << "    @" << inst.id << " = " << read_parse_instance(inst) << "\n";
      out << "    @_io.seek(_pos)\n";
    } else {
      out << "    @" << inst.id << " = " << expr(inst.value_expr, -1) << "\n";
    }
    out << "    @" << inst.id << "\n  end\n";
    known_instances.insert(inst.id);
    first_instance = false;
  }
  out << "end\n";
  return out.str();
}

std::string RenderLuaModule(const ir::Spec& spec, bool wireshark_mode) {
  const std::string class_name = ToUpperCamelIdentifier(spec.name);
  const auto user_types = BuildUserTypeMap(spec);
  std::set<std::string> attrs;
  for (const auto& a : spec.attrs) attrs.insert(a.id);
  std::set<std::string> known_instances;
  std::set<std::string> property_instances;
  bool needs_str_decode = false;
  for (const auto& inst : spec.instances) {
    if (inst.kind == ir::Instance::Kind::kParse) property_instances.insert(inst.id);
    if (inst.kind != ir::Instance::Kind::kParse) continue;
    const auto primitive = ResolvePrimitiveType(inst.type, user_types).value_or(ir::PrimitiveType::kU1);
    if (primitive == ir::PrimitiveType::kStr) {
      needs_str_decode = true;
      break;
    }
  }

  std::function<std::string(const ir::Expr&, int, const std::string&)> expr;
  expr = [&](const ir::Expr& e, int parent_prec, const std::string& repeat_item) {
    switch (e.kind) {
    case ir::Expr::Kind::kInt: return std::to_string(e.int_value);
    case ir::Expr::Kind::kBool: return e.bool_value ? std::string("true") : std::string("false");
    case ir::Expr::Kind::kName:
      if (!repeat_item.empty() && e.text == "_") return repeat_item;
      if (attrs.find(e.text) != attrs.end()) return "self." + e.text;
      if (property_instances.find(e.text) != property_instances.end()) return "self." + e.text;
      if (known_instances.find(e.text) != known_instances.end()) return "self:" + e.text + "()";
      return e.text;
    case ir::Expr::Kind::kUnary: {
      std::string op = NormalizeOp(e.text);
      if (op == "!") return "(not " + expr(*e.lhs, 90, repeat_item) + ")";
      return "(" + op + expr(*e.lhs, 90, repeat_item) + ")";
    }
    case ir::Expr::Kind::kBinary: {
      std::string op = NormalizeOp(e.text);
      if (op == "&&") op = "and";
      if (op == "||") op = "or";
      const int prec = ExprPrecedence(e);
      std::string rendered = expr(*e.lhs, prec, repeat_item) + " " + op + " " + expr(*e.rhs, prec + 1, repeat_item);
      if (prec <= parent_prec) rendered = "(" + rendered + ")";
      return rendered;
    }
    }
    return std::string("0");
  };

  auto read_primitive = [&](ir::PrimitiveType primitive, std::optional<ir::Endian> override_endian) {
    if (primitive == ir::PrimitiveType::kBytes) return std::string("self._io:read_bytes_full()");
    if (primitive == ir::PrimitiveType::kStr) return std::string("''");
    return std::string("self._io:") + ReadMethod(primitive, override_endian.value_or(spec.default_endian)) + "()";
  };

  auto read_parse_instance = [&](const ir::Instance& inst) {
    const auto primitive = ResolvePrimitiveType(inst.type, user_types).value_or(ir::PrimitiveType::kU1);
    if (primitive == ir::PrimitiveType::kBytes) {
      if (inst.size_expr.has_value()) return "self._io:read_bytes(" + expr(*inst.size_expr, -1, "") + ")";
      return std::string("self._io:read_bytes_full()");
    }
    if (primitive == ir::PrimitiveType::kStr) {
      if (!inst.size_expr.has_value()) return std::string("\"\"");
      return "str_decode.decode(self._io:read_bytes(" + expr(*inst.size_expr, -1, "") + "), \"" +
             inst.encoding.value_or("UTF-8") + "\")";
    }
    return "self._io:" + ReadMethod(primitive, inst.endian_override.value_or(spec.default_endian)) + "()";
  };

  std::ostringstream out;
  out << "-- This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild\n";
  out << "--\n";
  out << "-- This file is compatible with Lua 5.3\n";
  out << "\n";
  out << class_name << " = " << class_name << " or {}\n";
  out << "local class = require(\"class\")\n";
  out << "if _G[\"KaitaiStruct\"] == nil then require(\"kaitaistruct\") end\n";
  if (needs_str_decode) out << "local str_decode = require(\"string_decode\")\n";
  out << "\n";
  out << class_name << " = class.class(KaitaiStruct)\n\n";
  out << "function " << class_name << ":_init(io, _parent, _root)\n";
  out << "  KaitaiStruct._init(self, io)\n";
  out << "  self._parent = _parent\n";
  out << "  self._root = _root or self\n";
  out << "  self:_read()\n";
  out << "end\n\n";
  out << "function " << class_name << ":_read()\n";

  for (const auto& attr : spec.attrs) {
    const auto primitive = ResolvePrimitiveType(attr.type, user_types).value_or(ir::PrimitiveType::kU1);
    auto render_read = [&]() {
      std::string read;
      if (primitive == ir::PrimitiveType::kBytes) {
        read = attr.size_expr.has_value() ? ("self._io:read_bytes(" + expr(*attr.size_expr, -1, "") + ")") : "self._io:read_bytes_full()";
        if (attr.process.has_value() && attr.process->kind == ir::Attr::Process::Kind::kXorConst) {
          read = "KaitaiStream.process_xor_one(" + read + ", " + std::to_string(attr.process->xor_const) + ")";
        }
      } else {
        read = read_primitive(primitive, attr.endian_override);
      }
      return read;
    };

    if (attr.repeat == ir::Attr::RepeatKind::kExpr) {
      out << "  self." << attr.id << " = {}\n";
      out << "  for i = 1, " << expr(*attr.repeat_expr, -1, "") << " do\n";
      out << "    self." << attr.id << "[i] = " << render_read() << "\n";
      out << "  end\n";
    } else if (attr.repeat == ir::Attr::RepeatKind::kEos) {
      out << "  self." << attr.id << " = {}\n";
      out << "  while not self._io:is_eof() do\n";
      out << "    self." << attr.id << "[#self." << attr.id << " + 1] = " << render_read() << "\n";
      out << "  end\n";
    } else if (attr.repeat == ir::Attr::RepeatKind::kUntil) {
      out << "  self." << attr.id << " = {}\n";
      out << "  while true do\n";
      out << "    local repeat_item = " << render_read() << "\n";
      out << "    self." << attr.id << "[#self." << attr.id << " + 1] = repeat_item\n";
      out << "    if " << expr(*attr.repeat_expr, -1, "repeat_item") << " then break end\n";
      out << "  end\n";
    } else {
      if (primitive == ir::PrimitiveType::kBytes &&
          attr.process.has_value() &&
          attr.process->kind == ir::Attr::Process::Kind::kXorConst) {
        const std::string raw_read = attr.size_expr.has_value() ? ("self._io:read_bytes(" + expr(*attr.size_expr, -1, "") + ")") : "self._io:read_bytes_full()";
        out << "  self._raw_" << attr.id << " = " << raw_read << "\n";
        out << "  self." << attr.id << " = KaitaiStream.process_xor_one(self._raw_" << attr.id << ", "
            << attr.process->xor_const << ")\n";
      } else {
        out << "  self." << attr.id << " = " << render_read() << "\n";
      }
    }
  }

  for (const auto& v : spec.validations) {
    bool emitted = false;
    if (v.condition_expr.kind == ir::Expr::Kind::kBinary && v.condition_expr.text == "==") {
      const auto is_target = [&](const ir::Expr& e) { return e.kind == ir::Expr::Kind::kName && e.text == v.target; };
      const auto is_int = [&](const ir::Expr& e) { return e.kind == ir::Expr::Kind::kInt; };
      const bool lhs_target_rhs_int = is_target(*v.condition_expr.lhs) && is_int(*v.condition_expr.rhs);
      const bool rhs_target_lhs_int = is_target(*v.condition_expr.rhs) && is_int(*v.condition_expr.lhs);
      if (lhs_target_rhs_int || rhs_target_lhs_int) {
        const long long expected = lhs_target_rhs_int ? v.condition_expr.rhs->int_value : v.condition_expr.lhs->int_value;
        out << "  if not(self." << v.target << " == " << expected << ") then\n";
        out << "    error(\"not equal, expected \" .. " << expected << " .. \", but got \" .. self." << v.target << ")\n";
        out << "  end\n";
        emitted = true;
      }
    }
    if (!emitted) {
      out << "  if not (" << expr(v.condition_expr, -1, "") << ") then error('validation failed: /valid/" << v.target << "') end\n";
    }
  }
  out << "end\n";

  for (const auto& inst : spec.instances) {
    if (inst.kind == ir::Instance::Kind::kParse) {
      out << "\n" << class_name << ".property." << inst.id << " = {}\n";
      out << "function " << class_name << ".property." << inst.id << ":get()\n";
      out << "  if self._m_" << inst.id << " ~= nil then\n";
      out << "    return self._m_" << inst.id << "\n";
      out << "  end\n\n";
      out << "  local _pos = self._io:pos()\n";
      if (inst.pos_expr.has_value()) {
        out << "  self._io:seek(" << expr(*inst.pos_expr, -1, "") << ")\n";
      }
      out << "  self._m_" << inst.id << " = " << read_parse_instance(inst) << "\n";
      out << "  self._io:seek(_pos)\n";
      out << "  return self._m_" << inst.id << "\n";
      out << "end\n";
    } else {
      out << "\nfunction " << class_name << ":" << inst.id << "()\n";
      out << "  if self._m_" << inst.id << " ~= nil then return self._m_" << inst.id << " end\n";
      out << "  self._m_" << inst.id << " = " << expr(inst.value_expr, -1, "") << "\n";
      out << "  return self._m_" << inst.id << "\n";
      out << "end\n";
    }
    known_instances.insert(inst.id);
  }

  if (wireshark_mode) {
    out << "\n";
    out << "-- Wireshark Lua dissector\n";
    out << "local " << spec.name << "_proto = Proto(\"" << spec.name << "\", \"" << class_name << "\")\n\n";
    out << "function " << spec.name << "_proto.dissector(tvb, pinfo, tree)\n";
    out << "  pinfo.cols.protocol = \"" << class_name << "\"\n";
    out << "  local subtree = tree:add(" << spec.name << "_proto, tvb())\n";
    out << "  local status, parsed = pcall(function()\n";
    out << "    return " << class_name << ":from_string(tvb:range():string())\n";
    out << "  end)\n";
    out << "  if not status then\n";
    out << "    subtree:add_expert_info(PI_MALFORMED, PI_ERROR, \"Kaitai Struct parse error: \" .. parsed)\n";
    out << "  end\n";
    out << "end\n\n";
    out << "-- Register the dissector on the desired port by setting this.\n";
    out << "local " << spec.name << "_proto_default_port = 0\n";
    out << "if " << spec.name << "_proto_default_port > 0 then\n";
    out << "  DissectorTable.get(\"tcp.port\"):add(" << spec.name << "_proto_default_port, " << spec.name << "_proto)\n";
    out << "end\n";
  }
  return out.str();
}

std::string RenderScriptingModule(const ir::Spec& spec, const std::string& target) {
  if (target == "python") return RenderPythonModule(spec);
  if (target == "ruby") return RenderRubyModule(spec);
  return RenderLuaModule(spec, target == "wireshark_lua");
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
                          std::filesystem::path(options.out_dir) / (spec.name + "_wireshark.lua"));
}

Result EmitPythonFromIr(const ir::Spec& spec, const CliOptions& options) {
  return EmitScriptTarget(spec, options, "python", PythonOutputPath(spec, options));
}

Result EmitRubyFromIr(const ir::Spec& spec, const CliOptions& options) {
  return EmitScriptTarget(spec, options, "ruby", std::filesystem::path(options.out_dir) / (spec.name + ".rb"));
}

} // namespace kscpp::codegen
