#include "codegen.h"

#include <array>
#include <algorithm>
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
std::string CppUserTypeName(const std::string& type_name);
bool IsUnresolvedUserType(const ir::TypeRef& type_ref,
                          const std::map<std::string, ir::TypeRef>& user_types);

bool ParseSpecialUnary(const std::string& op, const std::string& prefix, std::string* payload) {
  if (op.rfind(prefix, 0) != 0) return false;
  *payload = op.substr(prefix.size());
  return !payload->empty();
}

bool DecodeBase64(const std::string& input, std::string* output) {
  static const std::string kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::array<int, 256> table{};
  table.fill(-1);
  for (size_t i = 0; i < kAlphabet.size(); i++) {
    table[static_cast<unsigned char>(kAlphabet[i])] = static_cast<int>(i);
  }

  int val = 0;
  int valb = -8;
  std::string out;
  out.reserve((input.size() * 3) / 4);
  for (unsigned char c : input) {
    if (std::isspace(c) != 0) continue;
    if (c == '=') break;
    const int decoded = table[c];
    if (decoded < 0) return false;
    val = (val << 6) + decoded;
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<char>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  *output = std::move(out);
  return true;
}

bool IsEmbeddedScopeRef(const ir::TypeRef& ref, std::string* encoded_payload) {
  if (ref.kind != ir::TypeRef::Kind::kUser) return false;
  static const std::string kPrefix = "__scope_b64__:";
  if (ref.user_type.rfind(kPrefix, 0) != 0) return false;
  *encoded_payload = ref.user_type.substr(kPrefix.size());
  return !encoded_payload->empty();
}

std::map<std::string, ir::Spec> DecodeEmbeddedScopes(const ir::Spec& spec) {
  std::map<std::string, ir::Spec> scopes;
  for (const auto& t : spec.types) {
    std::string encoded;
    if (!IsEmbeddedScopeRef(t.type, &encoded)) continue;
    std::string decoded;
    if (!DecodeBase64(encoded, &decoded)) continue;
    ir::Spec scope_spec;
    auto parse = ir::Deserialize(decoded, &scope_spec, false);
    if (!parse.ok) continue;
    scopes[t.name] = scope_spec;
  }
  return scopes;
}

std::vector<std::string> SplitScopePath(const std::string& name) {
  if (name.empty()) return {};
  std::vector<std::string> out;
  size_t start = 0;
  while (start < name.size()) {
    size_t pos = name.find("::", start);
    if (pos == std::string::npos) {
      out.push_back(name.substr(start));
      break;
    }
    out.push_back(name.substr(start, pos - start));
    start = pos + 2;
  }
  return out;
}

std::string JoinScopePath(const std::vector<std::string>& parts, size_t upto) {
  std::ostringstream out;
  for (size_t i = 0; i < upto; i++) {
    if (i > 0) out << "::";
    out << parts[i];
  }
  return out.str();
}


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

std::string ImportStem(const std::string& import_name) {
  std::string value = import_name;
  const size_t slash = value.find_last_of("/\\");
  if (slash != std::string::npos) {
    value = value.substr(slash + 1);
  }
  const size_t dot = value.find_last_of('.');
  if (dot != std::string::npos) {
    value = value.substr(0, dot);
  }
  return value;
}

bool UserTypeMatchesImport(const std::string& type_name, const std::string& import_stem) {
  if (type_name == import_stem) return true;
  if (type_name.size() > import_stem.size() &&
      type_name.compare(type_name.size() - import_stem.size(), import_stem.size(), import_stem) == 0 &&
      type_name[type_name.size() - import_stem.size() - 1] == ':') {
    return true;
  }
  return false;
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
  {
    std::string payload;
    if (ParseSpecialUnary(expr.text, "__cast__:", &payload)) {
      return "static_cast<" + CppUserTypeName(payload) + "*>(" +
             RenderExpr(*expr.lhs, attrs, instances, 90, repeat_item_name) + ")";
    }
    if (ParseSpecialUnary(expr.text, "__attr__:", &payload)) {
      return RenderExpr(*expr.lhs, attrs, instances, 90, repeat_item_name) + "->" + payload + "()";
    }
    return "(" + NormalizeOp(expr.text) + RenderExpr(*expr.lhs, attrs, instances, 90, repeat_item_name) + ")";
  }
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
  for (const auto& param : spec.params) known_names.insert(param.id);
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
      if (expr.text != "-" && expr.text != "!" && expr.text != "not" && expr.text != "~" &&
          expr.text.rfind("__cast__:", 0) != 0 && expr.text.rfind("__attr__:", 0) != 0) {
        return {false, "not yet supported: unary operator \"" + expr.text + "\""};
      }
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
      const bool unresolved_user_type =
          !resolved_type.has_value() && inst.type.kind == ir::TypeRef::Kind::kUser;
      if (!resolved_type.has_value() && !unresolved_user_type) {
        return {false, "not yet supported: parse instance type must resolve to primitive type"};
      }
      if (unresolved_user_type && inst.encoding.has_value()) {
        return {false, "not yet supported: encoding on user-type parse instances"};
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
    for (const auto& arg : attr.user_type_args) {
      auto result = validate_expr(arg);
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
  auto rank = [](ir::PrimitiveType t) -> int {
    switch (t) {
    case ir::PrimitiveType::kU1:
    case ir::PrimitiveType::kS1:
      return 1;
    case ir::PrimitiveType::kU2:
    case ir::PrimitiveType::kS2:
      return 2;
    case ir::PrimitiveType::kU4:
    case ir::PrimitiveType::kS4:
    case ir::PrimitiveType::kF4:
      return 4;
    case ir::PrimitiveType::kU8:
    case ir::PrimitiveType::kS8:
    case ir::PrimitiveType::kF8:
      return 8;
    case ir::PrimitiveType::kBytes:
    case ir::PrimitiveType::kStr:
      return 100;
    }
    return 1;
  };
  ir::PrimitiveType selected = ir::PrimitiveType::kU1;
  int selected_rank = -1;
  for (const auto& c : attr.switch_cases) {
    const auto primitive = ResolvePrimitiveType(c.type, user_types).value_or(ir::PrimitiveType::kU1);
    const int r = rank(primitive);
    if (r > selected_rank) {
      selected = primitive;
      selected_rank = r;
    }
  }
  return CppFieldType(selected);
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
  if (attr.type.kind == ir::TypeRef::Kind::kUser &&
      !ResolvePrimitiveType(attr.type, user_types).has_value()) {
    return CppUserTypeName(attr.type.user_type) + "*";
  }
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
  const auto primitive = ResolvePrimitiveType(attr.type, user_types);
  if (!primitive.has_value() && IsUnresolvedUserType(attr.type, user_types)) {
    const std::string type_name = CppUserTypeName(attr.type.user_type);
    std::ostringstream ctor_args;
    const bool local_alias = user_types.find(attr.type.user_type) != user_types.end();
    if (local_alias) {
      ctor_args << "m__io, this, m__root";
    } else {
      bool first = true;
      for (const auto& arg : attr.user_type_args) {
        if (!first) ctor_args << ", ";
        ctor_args << RenderExpr(arg, attrs, instances, -1);
        first = false;
      }
      if (!first) ctor_args << ", ";
      ctor_args << "m__io";
    }
    return "std::unique_ptr<" + type_name + ">(new " + type_name + "(" + ctor_args.str() + "))";
  }
  const auto primitive_kind = primitive.value_or(ir::PrimitiveType::kU1);
  if (primitive_kind == ir::PrimitiveType::kBytes) {
    std::string read = attr.size_expr.has_value() ?
      ("m__io->read_bytes(" + RenderExpr(*attr.size_expr, attrs, instances, -1) + ")") :
      "m__io->read_bytes_full()";
    if (attr.process.has_value() && attr.process->kind == ir::Attr::Process::Kind::kXorConst) {
      read = "kaitai::kstream::process_xor_one(" + read + ", " + std::to_string(attr.process->xor_const) + ")";
    }
    return read;
  }
  if (primitive_kind == ir::PrimitiveType::kStr) {
    if (!attr.size_expr.has_value()) return "std::string()";
    const std::string enc = attr.encoding.value_or("UTF-8");
    return "kaitai::kstream::bytes_to_str(m__io->read_bytes(" + RenderExpr(*attr.size_expr, attrs, instances, -1) + "), \"" + enc + "\")";
  }
  std::string base = CppReadPrimitiveExpr(primitive_kind, attr.endian_override, default_endian);
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
  const bool unresolved_user =
      IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value();
  if (unresolved_user) {
    const std::string type_name = CppUserTypeName(attr.type.user_type);
    if (attr.repeat != ir::Attr::RepeatKind::kNone) {
      return "std::unique_ptr<std::vector<std::unique_ptr<" + type_name + ">>>";
    }
    return "std::unique_ptr<" + type_name + ">";
  }
  std::string base = attr.switch_on.has_value() ? SwitchCaseType(attr, user_types) : CppAttrType(attr, user_types);
  if (attr.repeat != ir::Attr::RepeatKind::kNone) return "std::unique_ptr<std::vector<" + base + ">>";
  return base;
}

std::string CppRepeatElementType(const ir::Attr& attr,
                                 const std::map<std::string, ir::TypeRef>& user_types) {
  const bool unresolved_user =
      IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value();
  if (unresolved_user) {
    return "std::unique_ptr<" + CppUserTypeName(attr.type.user_type) + ">";
  }
  return attr.switch_on.has_value() ? SwitchCaseType(attr, user_types) : CppAttrType(attr, user_types);
}

std::string CppAccessorType(const ir::Attr& attr, const std::map<std::string, ir::TypeRef>& user_types) {
  const bool unresolved_user =
      IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value();
  if (unresolved_user) {
    const std::string type_name = CppUserTypeName(attr.type.user_type);
    if (attr.repeat != ir::Attr::RepeatKind::kNone) {
      return "std::vector<std::unique_ptr<" + type_name + ">>*";
    }
    return type_name + "*";
  }
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
  if (type_name == "kaitai::kstruct" || type_name == "struct") return "kaitai::kstruct";
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

bool IsUnresolvedUserType(const ir::TypeRef& type_ref,
                          const std::map<std::string, ir::TypeRef>& user_types) {
  return type_ref.kind == ir::TypeRef::Kind::kUser &&
         !ResolvePrimitiveType(type_ref, user_types).has_value();
}

std::string CppInstanceType(const ir::Instance& inst,
                            const std::map<std::string, ExprType>& instance_types,
                            const std::map<std::string, ir::TypeRef>& user_types) {
  if (inst.kind == ir::Instance::Kind::kParse) {
    return CppTypeForTypeRef(inst.type, user_types);
  }
  if (inst.has_explicit_type) {
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
  const auto resolved = ResolvePrimitiveType(inst.type, user_types);
  if (!resolved.has_value() && inst.type.kind == ir::TypeRef::Kind::kUser) {
    const bool local_alias = user_types.find(inst.type.user_type) != user_types.end();
    if (local_alias) {
      return "new " + CppUserTypeName(inst.type.user_type) + "(m__io, this, m__root)";
    }
    return "new " + CppUserTypeName(inst.type.user_type) + "(m__io)";
  }
  const auto primitive = resolved.value_or(ir::PrimitiveType::kU1);
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

std::string Indent(int level) { return std::string(level * 4, ' '); }

std::string LastScopeSegment(const std::string& scope_name) {
  auto parts = SplitScopePath(scope_name);
  if (parts.empty()) return scope_name;
  return parts.back();
}

std::string ParentScopeName(const std::string& scope_name) {
  auto parts = SplitScopePath(scope_name);
  if (parts.size() <= 1) return "";
  return JoinScopePath(parts, parts.size() - 1);
}

std::string CppScopeTypeQualified(const std::string& root_name, const std::string& scope_name) {
  std::ostringstream out;
  out << root_name << "_t";
  for (const auto& part : SplitScopePath(scope_name)) {
    out << "::" << part << "_t";
  }
  return out.str();
}

std::vector<std::string> DirectChildScopes(const std::map<std::string, ir::Spec>& scopes,
                                           const std::string& parent_scope) {
  std::vector<std::string> out;
  for (const auto& kv : scopes) {
    const std::string& scope_name = kv.first;
    if (ParentScopeName(scope_name) == parent_scope) out.push_back(scope_name);
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::optional<std::string> ResolveScopeRef(const std::string& ref,
                                           const std::string& root_name,
                                           const std::map<std::string, ir::Spec>& scopes) {
  if (scopes.find(ref) != scopes.end()) return ref;
  const std::string rooted = root_name + "::";
  if (ref.rfind(rooted, 0) == 0) {
    std::string rel = ref.substr(rooted.size());
    if (scopes.find(rel) != scopes.end()) return rel;
  }
  for (const auto& kv : scopes) {
    const std::string& scope_name = kv.first;
    if (scope_name == ref) return scope_name;
    if (scope_name.size() > ref.size() &&
        scope_name.compare(scope_name.size() - ref.size(), ref.size(), ref) == 0 &&
        scope_name[scope_name.size() - ref.size() - 1] == ':') {
      return scope_name;
    }
  }
  return std::nullopt;
}

std::string UpperSnake(const std::string& value) {
  std::string out;
  for (char c : value) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) != 0) {
      out.push_back(static_cast<char>(std::toupper(uc)));
    } else {
      out.push_back('_');
    }
  }
  if (out.empty() || (out[0] >= '0' && out[0] <= '9')) out.insert(out.begin(), '_');
  return out;
}

std::string EnumShortName(const std::string& enum_name) {
  const auto pos = enum_name.rfind("::");
  return pos == std::string::npos ? enum_name : enum_name.substr(pos + 2);
}

std::string NestedEnumTypeName(const std::string& enum_name) { return EnumShortName(enum_name) + "_t"; }

std::string NestedEnumValueName(const std::string& enum_name, const std::string& value_name) {
  return UpperSnake(EnumShortName(enum_name)) + "_" + UpperSnake(value_name);
}

bool ScopeHasEnumName(const ir::Spec& scope_spec, const std::string& enum_name) {
  for (const auto& e : scope_spec.enums) {
    if (EnumShortName(e.name) == EnumShortName(enum_name)) return true;
  }
  return false;
}

std::optional<std::string> ResolveEnumOwnerScope(const std::string& current_scope,
                                                 const std::string& enum_name,
                                                 const std::map<std::string, ir::Spec>& scopes) {
  std::string s = current_scope;
  while (true) {
    auto it = scopes.find(s);
    if (it != scopes.end() && ScopeHasEnumName(it->second, enum_name)) return s;
    if (s.empty()) break;
    s = ParentScopeName(s);
  }
  return std::nullopt;
}

std::string ScopeParentCppPtrType(const std::string& root_name, const std::string& scope_name) {
  const std::string parent = ParentScopeName(scope_name);
  if (parent.empty()) return root_name + "_t*";
  return CppScopeTypeQualified(root_name, parent) + "*";
}

std::string ScopeLocalTypeToken(const std::string& root_name,
                                const std::string& current_scope,
                                const std::string& target_scope) {
  const std::string target_parent = ParentScopeName(target_scope);
  if (target_parent == current_scope || target_scope == current_scope) {
    return LastScopeSegment(target_scope) + "_t";
  }
  return CppScopeTypeQualified(root_name, target_scope);
}

bool HasSwitchElseCase(const ir::Attr& attr) {
  for (const auto& c : attr.switch_cases) {
    if (!c.match_expr.has_value()) return true;
  }
  return false;
}

std::string NestedAttrBaseType(const ir::Attr& attr,
                               const std::string& current_scope,
                               const std::string& root_name,
                               const std::map<std::string, ir::Spec>& scopes,
                               const std::map<std::string, ir::TypeRef>& user_types) {
  if (attr.enum_name.has_value()) {
    return NestedEnumTypeName(*attr.enum_name);
  }
  if (IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value()) {
    const auto resolved = ResolveScopeRef(attr.type.user_type, root_name, scopes);
    const std::string type_expr = resolved.has_value()
                                      ? ScopeLocalTypeToken(root_name, current_scope, *resolved)
                                      : CppUserTypeName(attr.type.user_type);
    return type_expr + "*";
  }
  if (attr.switch_on.has_value()) return SwitchCaseType(attr, user_types);
  const auto primitive = ResolvePrimitiveType(attr.type, user_types).value_or(ir::PrimitiveType::kU1);
  return CppFieldType(primitive);
}

std::string NestedAttrStorageType(const ir::Attr& attr,
                                  const std::string& current_scope,
                                  const std::string& root_name,
                                  const std::map<std::string, ir::Spec>& scopes,
                                  const std::map<std::string, ir::TypeRef>& user_types) {
  if (attr.repeat != ir::Attr::RepeatKind::kNone) {
    if (IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value()) {
      const auto resolved = ResolveScopeRef(attr.type.user_type, root_name, scopes);
      const std::string type_expr = resolved.has_value()
                                        ? ScopeLocalTypeToken(root_name, current_scope, *resolved)
                                        : CppUserTypeName(attr.type.user_type);
      return "std::unique_ptr<std::vector<std::unique_ptr<" + type_expr + ">>>";
    }
    return "std::unique_ptr<std::vector<" + NestedAttrBaseType(attr, current_scope, root_name, scopes, user_types) + ">>";
  }
  if (IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value()) {
    const auto resolved = ResolveScopeRef(attr.type.user_type, root_name, scopes);
    const std::string type_expr = resolved.has_value()
                                      ? ScopeLocalTypeToken(root_name, current_scope, *resolved)
                                      : CppUserTypeName(attr.type.user_type);
    return "std::unique_ptr<" + type_expr + ">";
  }
  return NestedAttrBaseType(attr, current_scope, root_name, scopes, user_types);
}

std::string NestedAttrAccessorType(const ir::Attr& attr,
                                   const std::string& current_scope,
                                   const std::string& root_name,
                                   const std::map<std::string, ir::Spec>& scopes,
                                   const std::map<std::string, ir::TypeRef>& user_types) {
  if (attr.repeat != ir::Attr::RepeatKind::kNone) {
    if (IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value()) {
      const auto resolved = ResolveScopeRef(attr.type.user_type, root_name, scopes);
      const std::string type_expr = resolved.has_value()
                                        ? ScopeLocalTypeToken(root_name, current_scope, *resolved)
                                        : CppUserTypeName(attr.type.user_type);
      return "std::vector<std::unique_ptr<" + type_expr + ">>*";
    }
    return "std::vector<" + NestedAttrBaseType(attr, current_scope, root_name, scopes, user_types) + ">*";
  }
  if (IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value()) {
    const auto resolved = ResolveScopeRef(attr.type.user_type, root_name, scopes);
    const std::string type_expr = resolved.has_value()
                                      ? ScopeLocalTypeToken(root_name, current_scope, *resolved)
                                      : CppUserTypeName(attr.type.user_type);
    return type_expr + "*";
  }
  return NestedAttrBaseType(attr, current_scope, root_name, scopes, user_types);
}

void EmitNestedClassHeader(std::ostringstream* out,
                           const std::string& root_name,
                           const std::string& scope_name,
                           const std::map<std::string, ir::Spec>& scopes,
                           const std::map<std::string, ir::TypeRef>& user_types,
                           int indent);

void EmitNestedClassSource(std::ostringstream* out,
                           const std::string& root_name,
                           const std::string& scope_name,
                           const std::map<std::string, ir::Spec>& scopes,
                           const std::map<std::string, ir::TypeRef>& user_types);

void EmitNestedClassHeader(std::ostringstream* out,
                           const std::string& root_name,
                           const std::string& scope_name,
                           const std::map<std::string, ir::Spec>& scopes,
                           const std::map<std::string, ir::TypeRef>& user_types,
                           int indent) {
  const auto it = scopes.find(scope_name);
  if (it == scopes.end()) return;
  const ir::Spec& scope_spec = it->second;
  const std::string class_name = LastScopeSegment(scope_name) + "_t";
  const std::string parent_ptr_type = ScopeParentCppPtrType(root_name, scope_name);
  const auto children = DirectChildScopes(scopes, scope_name);
  const bool has_enums = !scope_spec.enums.empty();
  const std::string ind = Indent(indent);
  const std::string ind1 = Indent(indent + 1);

  *out << ind << "class " << class_name << " : public kaitai::kstruct {\n\n";
  *out << ind << "public:\n";
  for (const auto& child : children) {
    *out << ind1 << "class " << LastScopeSegment(child) << "_t;\n";
  }
  if (!children.empty()) *out << "\n";

  for (const auto& e : scope_spec.enums) {
    const std::string enum_ty = NestedEnumTypeName(e.name);
    *out << ind1 << "enum " << enum_ty << " {\n";
    for (size_t i = 0; i < e.values.size(); i++) {
      const auto& v = e.values[i];
      *out << Indent(indent + 2) << NestedEnumValueName(e.name, v.name) << " = " << v.value
           << (i + 1 == e.values.size() ? "\n" : ",\n");
    }
    *out << ind1 << "};\n";
    *out << ind1 << "static bool _is_defined_" << enum_ty << "(" << enum_ty << " v);\n\n";
    *out << ind << "private:\n";
    *out << ind1 << "static const std::set<" << enum_ty << "> _values_" << enum_ty << ";\n\n";
    *out << ind << "public:\n";
    *out << "\n";
  }

  if (children.empty() && !has_enums) {
    *out << "\n";
  }

  *out << ind1 << class_name << "(kaitai::kstream* p__io, " << parent_ptr_type
       << " p__parent = nullptr, " << root_name << "_t* p__root = nullptr);\n\n";
  *out << ind << "private:\n";
  *out << ind1 << "void _read();\n";
  *out << ind1 << "void _clean_up();\n\n";
  *out << ind << "public:\n";
  *out << ind1 << "~" << class_name << "();\n";

  for (const auto& child : children) {
    *out << "\n";
    EmitNestedClassHeader(out, root_name, child, scopes, user_types, indent + 1);
  }
  if (!children.empty()) {
    *out << "\n";
    *out << ind << "public:\n";
  }

  for (const auto& attr : scope_spec.attrs) {
    const std::string access_type =
        NestedAttrAccessorType(attr, scope_name, root_name, scopes, user_types);
    if (attr.repeat != ir::Attr::RepeatKind::kNone ||
        (IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value())) {
      *out << ind1 << access_type << " " << attr.id << "() const { return m_" << attr.id
           << ".get(); }\n";
    } else {
      *out << ind1 << access_type << " " << attr.id << "() const { return m_" << attr.id
           << "; }\n";
    }
  }
  *out << ind1 << root_name << "_t* _root() const { return m__root; }\n";
  *out << ind1 << parent_ptr_type << " _parent() const { return m__parent; }\n";

  *out << "\n";
  *out << ind << "private:\n";
  bool has_nullable_switch = false;
  for (const auto& attr : scope_spec.attrs) {
    *out << ind1
         << NestedAttrStorageType(attr, scope_name, root_name, scopes, user_types) << " m_"
         << attr.id << ";\n";
    if (attr.switch_on.has_value() && !HasSwitchElseCase(attr)) {
      has_nullable_switch = true;
      *out << ind1 << "bool n_" << attr.id << ";\n";
    }
  }
  if (has_nullable_switch) {
    *out << "\n";
    *out << ind << "public:\n";
    for (const auto& attr : scope_spec.attrs) {
      if (attr.switch_on.has_value() && !HasSwitchElseCase(attr)) {
        *out << ind1 << "bool _is_null_" << attr.id << "() { " << attr.id << "(); return n_"
             << attr.id << "; };\n";
      }
    }
    *out << "\n";
    *out << ind << "private:\n";
  }
  *out << ind1 << root_name << "_t* m__root;\n";
  *out << ind1 << parent_ptr_type << " m__parent;\n";
  *out << ind << "};\n";
}

void EmitNestedClassSource(std::ostringstream* out,
                           const std::string& root_name,
                           const std::string& scope_name,
                           const std::map<std::string, ir::Spec>& scopes,
                           const std::map<std::string, ir::TypeRef>& user_types) {
  const auto it = scopes.find(scope_name);
  if (it == scopes.end()) return;
  const ir::Spec& scope_spec = it->second;
  const std::string class_name = LastScopeSegment(scope_name) + "_t";
  const std::string full_class = CppScopeTypeQualified(root_name, scope_name);
  const std::string parent_ptr_type = ScopeParentCppPtrType(root_name, scope_name);

  std::set<std::string> attrs;
  for (const auto& a : scope_spec.attrs) attrs.insert(a.id);
  std::set<std::string> instances;

  auto enum_cast_type = [&](const std::string& enum_name) {
    auto owner = ResolveEnumOwnerScope(scope_name, enum_name, scopes);
    if (owner.has_value()) {
      return CppScopeTypeQualified(root_name, *owner) + "::" + NestedEnumTypeName(enum_name);
    }
    return NestedEnumTypeName(enum_name);
  };

  auto read_scope_user = [&](const ir::Attr& attr) {
    const auto resolved = ResolveScopeRef(attr.type.user_type, root_name, scopes);
    std::string type_expr = resolved.has_value()
                                ? ScopeLocalTypeToken(root_name, scope_name, *resolved)
                                : CppUserTypeName(attr.type.user_type);
    std::ostringstream ctor_args;
    bool first = true;
    for (const auto& arg : attr.user_type_args) {
      if (!first) ctor_args << ", ";
      ctor_args << RenderExpr(arg, attrs, instances, -1);
      first = false;
    }
    if (!first) ctor_args << ", ";
    ctor_args << "m__io, this, m__root";
    return "std::unique_ptr<" + type_expr + ">(new " + type_expr + "(" + ctor_args.str() + "))";
  };

  for (const auto& e : scope_spec.enums) {
    const std::string enum_ty = NestedEnumTypeName(e.name);
    *out << "const std::set<" << full_class << "::" << enum_ty << "> " << full_class
         << "::_values_" << enum_ty << "{\n";
    for (size_t i = 0; i < e.values.size(); i++) {
      const auto& v = e.values[i];
      (void)i;
      *out << "    " << full_class << "::" << NestedEnumValueName(e.name, v.name) << ",\n";
    }
    *out << "};\n";
    *out << "bool " << full_class << "::_is_defined_" << enum_ty << "(" << full_class << "::"
         << enum_ty << " v) {\n";
    *out << "    return " << full_class << "::_values_" << enum_ty << ".find(v) != " << full_class
         << "::_values_" << enum_ty << ".end();\n";
    *out << "}\n\n";
  }

  *out << full_class << "::" << class_name << "(kaitai::kstream* p__io, " << parent_ptr_type
       << " p__parent, " << root_name << "_t* p__root) : kaitai::kstruct(p__io) {\n";
  *out << "    m__parent = p__parent;\n";
  *out << "    m__root = p__root;\n";
  for (const auto& attr : scope_spec.attrs) {
    if (attr.repeat != ir::Attr::RepeatKind::kNone ||
        (IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value())) {
      *out << "    m_" << attr.id << " = nullptr;\n";
    }
  }
  *out << "    _read();\n";
  *out << "}\n\n";

  *out << "void " << full_class << "::_read() {\n";
  for (const auto& attr : scope_spec.attrs) {
    if (attr.switch_on.has_value() && attr.repeat == ir::Attr::RepeatKind::kNone) {
      const bool has_else = HasSwitchElseCase(attr);
      if (!has_else) {
        *out << "    n_" << attr.id << " = true;\n";
      }
      *out << "    switch (" << RenderExpr(*attr.switch_on, attrs, instances, -1) << ") {\n";
      for (const auto& c : attr.switch_cases) {
        if (!c.match_expr.has_value()) {
          *out << "    default: {\n";
        } else {
          *out << "    case " << c.match_expr->int_value << ": {\n";
        }
        if (!has_else || !c.match_expr.has_value()) {
          *out << "        n_" << attr.id << " = false;\n";
        }
        const auto case_primitive = ResolvePrimitiveType(c.type, user_types).value_or(ir::PrimitiveType::kU1);
        *out << "        m_" << attr.id << " = "
             << CppReadPrimitiveExpr(case_primitive, attr.endian_override, scope_spec.default_endian)
             << ";\n";
        *out << "        break;\n";
        *out << "    }\n";
      }
      *out << "    }\n";
      continue;
    }

    if (attr.repeat == ir::Attr::RepeatKind::kNone) {
      if (IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value()) {
        *out << "    m_" << attr.id << " = " << read_scope_user(attr) << ";\n";
      } else if (attr.enum_name.has_value()) {
        const auto primitive = ResolvePrimitiveType(attr.type, user_types).value_or(ir::PrimitiveType::kU1);
        *out << "    m_" << attr.id << " = static_cast<" << enum_cast_type(*attr.enum_name) << ">("
             << CppReadPrimitiveExpr(primitive, attr.endian_override, scope_spec.default_endian) << ");\n";
      } else {
        *out << "    m_" << attr.id << " = "
             << ReadExpr(attr, scope_spec.default_endian, attrs, instances, user_types) << ";\n";
      }
      continue;
    }

    const std::string repeat_elem =
        IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value()
            ? ("std::unique_ptr<" +
               (ResolveScopeRef(attr.type.user_type, root_name, scopes).has_value()
                    ? ScopeLocalTypeToken(root_name, scope_name,
                                          *ResolveScopeRef(attr.type.user_type, root_name, scopes))
                    : CppUserTypeName(attr.type.user_type)) +
               ">")
            : NestedAttrBaseType(attr, scope_name, root_name, scopes, user_types);

    *out << "    m_" << attr.id << " = std::unique_ptr<std::vector<" << repeat_elem
         << ">>(new std::vector<" << repeat_elem << ">());\n";
    if (attr.repeat == ir::Attr::RepeatKind::kEos) {
      *out << "    while (!m__io->is_eof()) {\n";
      if (IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value()) {
        *out << "        m_" << attr.id << "->push_back(" << read_scope_user(attr) << ");\n";
      } else {
        *out << "        m_" << attr.id << "->push_back("
             << ReadExpr(attr, scope_spec.default_endian, attrs, instances, user_types) << ");\n";
      }
      *out << "    }\n";
    } else if (attr.repeat == ir::Attr::RepeatKind::kExpr) {
      *out << "    const int l_" << attr.id << " = " << RenderExpr(*attr.repeat_expr, attrs, instances, -1)
           << ";\n";
      *out << "    for (int i = 0; i < l_" << attr.id << "; i++) {\n";
      if (IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value()) {
        *out << "        m_" << attr.id << "->push_back(" << read_scope_user(attr) << ");\n";
      } else {
        *out << "        m_" << attr.id << "->push_back(std::move("
             << ReadExpr(attr, scope_spec.default_endian, attrs, instances, user_types) << "));\n";
      }
      *out << "    }\n";
    } else {
      *out << "    do {\n";
      if (IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value()) {
        *out << "        auto repeat_item = " << read_scope_user(attr) << ";\n";
      } else {
        *out << "        auto repeat_item = "
             << ReadExpr(attr, scope_spec.default_endian, attrs, instances, user_types) << ";\n";
      }
      *out << "        m_" << attr.id << "->push_back(std::move(repeat_item));\n";
      *out << "    } while (!("
           << RenderExpr(*attr.repeat_expr, attrs, instances, -1, "repeat_item") << "));\n";
    }
  }
  *out << "}\n\n";

  *out << full_class << "::~" << class_name << "() {\n";
  *out << "    _clean_up();\n";
  *out << "}\n\n";

  *out << "void " << full_class << "::_clean_up() {\n";
  for (const auto& attr : scope_spec.attrs) {
    if (attr.switch_on.has_value() && !HasSwitchElseCase(attr)) {
      *out << "    if (!n_" << attr.id << ") {\n";
      *out << "    }\n";
    }
  }
  *out << "}\n";

  *out << "\n";

  for (const auto& child : DirectChildScopes(scopes, scope_name)) {
    EmitNestedClassSource(out, root_name, child, scopes, user_types);
  }
}

std::string RenderHeader(const ir::Spec& spec) {
  const auto instance_types = ComputeInstanceTypes(spec);
  const auto user_types = BuildUserTypeMap(spec);
  const auto local_scopes = DecodeEmbeddedScopes(spec);
  std::set<std::string> required_import_headers;
  const auto maybe_add_import = [&](const ir::TypeRef& type_ref) {
    if (!IsUnresolvedUserType(type_ref, user_types)) return;
    if (type_ref.user_type == "kaitai::kstruct" || type_ref.user_type == "struct") return;
    for (const auto& imp : spec.imports) {
      const std::string stem = ImportStem(imp);
      if (UserTypeMatchesImport(type_ref.user_type, stem)) {
        required_import_headers.insert(stem);
      }
    }
  };
  for (const auto& p : spec.params) maybe_add_import(p.type);
  for (const auto& a : spec.attrs) {
    maybe_add_import(a.type);
    for (const auto& c : a.switch_cases) maybe_add_import(c.type);
  }
  for (const auto& i : spec.instances) {
    if (i.kind == ir::Instance::Kind::kParse || i.has_explicit_type) {
      maybe_add_import(i.type);
    }
  }
  const auto ctor_param_decl = [&]() {
    std::ostringstream args;
    for (const auto& p : spec.params) {
      args << CppTypeForTypeRef(p.type, user_types) << " p_" << p.id << ", ";
    }
    args << "kaitai::kstream* p__io, kaitai::kstruct* p__parent = nullptr, " << spec.name
         << "_t* p__root = nullptr";
    return args.str();
  };
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
  bool needs_set_include = !spec.enums.empty();
  if (!needs_set_include) {
    for (const auto& kv : local_scopes) {
      if (!kv.second.enums.empty()) {
        needs_set_include = true;
        break;
      }
    }
  }
  if (needs_set_include) out << "#include <set>\n";
  std::set<std::string> emitted_imports;
  for (const auto& imp : spec.imports) {
    const std::string stem = ImportStem(imp);
    if (required_import_headers.find(stem) == required_import_headers.end()) continue;
    if (!emitted_imports.insert(stem).second) continue;
    out << "#include \"" << stem << ".h\"\n";
  }
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
  out << "public:\n";
  const auto root_children = DirectChildScopes(local_scopes, "");
  if (root_children.empty()) out << "\n";
  for (const auto& child : root_children) {
    out << "    class " << LastScopeSegment(child) << "_t;\n";
  }
  if (!root_children.empty()) out << "\n";
  out << "    " << spec.name << "_t(" << ctor_param_decl() << ");\n\n";
  out << "private:\n";
  out << "    void _read();\n";
  out << "    void _clean_up();\n\n";
  out << "public:\n";
  out << "    ~" << spec.name << "_t();\n";
  for (const auto& child : root_children) {
    out << "\n";
    EmitNestedClassHeader(&out, spec.name, child, local_scopes, user_types, 1);
  }
  if (!local_scopes.empty()) {
    out << "\npublic:\n";
  }
  std::vector<std::string> raw_accessors;
  std::vector<std::string> raw_fields;
  for (const auto& inst : spec.instances) {
    out << "    " << CppInstanceType(inst, instance_types, user_types) << " " << inst.id << "();\n";
  }
  for (const auto& p : spec.params) {
    out << "    " << CppTypeForTypeRef(p.type, user_types) << " " << p.id << "() const { return m_"
        << p.id << "; }\n";
  }
  for (const auto& attr : spec.attrs) {
    const bool unresolved_user = IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value();
    if (attr.repeat != ir::Attr::RepeatKind::kNone) {
      out << "    " << CppAccessorType(attr, user_types) << " " << attr.id << "() const { return m_" << attr.id << ".get(); }\n";
    } else if (unresolved_user) {
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
  for (const auto& p : spec.params) {
    out << "    " << CppTypeForTypeRef(p.type, user_types) << " m_" << p.id << ";\n";
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
  const auto local_scopes = DecodeEmbeddedScopes(spec);
  const auto ctor_param_decl = [&]() {
    std::ostringstream args;
    for (const auto& p : spec.params) {
      args << CppTypeForTypeRef(p.type, user_types) << " p_" << p.id << ", ";
    }
    args << "kaitai::kstream* p__io, kaitai::kstruct* p__parent, " << spec.name << "_t* p__root";
    return args.str();
  };
  std::set<std::string> attr_names;
  for (const auto& attr : spec.attrs) attr_names.insert(attr.id);
  for (const auto& p : spec.params) attr_names.insert(p.id);

  std::ostringstream out;
  out << "// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild\n\n";
  out << "#include \"" << spec.name << ".h\"\n";
  if (!spec.validations.empty()) {
    out << "#include \"kaitai/exceptions.h\"\n";
  }
  out << "\n";
  out << spec.name << "_t::" << spec.name << "_t(" << ctor_param_decl() << ") : kaitai::kstruct(p__io) {\n";
  out << "    m__parent = p__parent;\n";
  out << "    m__root = p__root ? p__root : this;\n";
  for (const auto& p : spec.params) {
    out << "    m_" << p.id << " = p_" << p.id << ";\n";
  }
  for (const auto& inst : spec.instances) out << "    f_" << inst.id << " = false;\n";
  for (const auto& attr : spec.attrs) {
    if (attr.repeat != ir::Attr::RepeatKind::kNone ||
        (IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value())) {
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
      const std::string repeat_elem = CppRepeatElementType(attr, user_types);
      out << indent << "m_" << attr.id << " = std::unique_ptr<std::vector<" << repeat_elem
          << ">>(new std::vector<" << repeat_elem << ">());\n";
      const bool unresolved_user =
          IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value();
      if (unresolved_user) {
        out << indent << "{\n";
        out << nested_indent << "int i = 0;\n";
        out << nested_indent << "while (!m__io->is_eof()) {\n";
        out << nested_indent << "    m_" << attr.id << "->push_back(std::move("
            << ReadExpr(attr, spec.default_endian, attr_names, {}, user_types) << "));\n";
        out << nested_indent << "    i++;\n";
        out << nested_indent << "}\n";
        out << indent << "}\n";
      } else {
        out << indent << "while (!m__io->is_eof()) {\n";
        if (attr.switch_on.has_value()) out << nested_indent << "m_" << attr.id << "->push_back(" << ReadSwitchExpr(attr, spec.default_endian, attr_names, {}, user_types) << ");\n";
        else out << nested_indent << "m_" << attr.id << "->push_back(" << ReadExpr(attr, spec.default_endian, attr_names, {}, user_types) << ");\n";
        out << indent << "}\n";
      }
    } else if (attr.repeat == ir::Attr::RepeatKind::kExpr) {
      const std::string repeat_elem = CppRepeatElementType(attr, user_types);
      out << indent << "m_" << attr.id << " = std::unique_ptr<std::vector<" << repeat_elem
          << ">>(new std::vector<" << repeat_elem << ">());\n";
      out << indent << "const int l_" << attr.id << " = " << RenderExpr(*attr.repeat_expr, attr_names, {}, -1) << ";\n";
      out << indent << "for (int i = 0; i < l_" << attr.id << "; i++) {\n";
      if (attr.switch_on.has_value()) out << nested_indent << "m_" << attr.id << "->push_back(std::move(" << ReadSwitchExpr(attr, spec.default_endian, attr_names, {}, user_types) << "));\n";
      else out << nested_indent << "m_" << attr.id << "->push_back(std::move(" << ReadExpr(attr, spec.default_endian, attr_names, {}, user_types) << "));\n";
      out << indent << "}\n";
    } else {
      const std::string repeat_elem = CppRepeatElementType(attr, user_types);
      out << indent << "m_" << attr.id << " = std::unique_ptr<std::vector<" << repeat_elem
          << ">>(new std::vector<" << repeat_elem << ">());\n";
      out << indent << "do {\n";
      if (attr.switch_on.has_value()) {
        out << nested_indent << "auto repeat_item = " << ReadSwitchExpr(attr, spec.default_endian, attr_names, {}, user_types) << ";\n";
      } else {
        out << nested_indent << "auto repeat_item = " << ReadExpr(attr, spec.default_endian, attr_names, {}, user_types) << ";\n";
      }
      out << nested_indent << "m_" << attr.id << "->push_back(std::move(repeat_item));\n";
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
    if (inst.kind != ir::Instance::Kind::kParse) continue;
    out << "    if (f_" << inst.id << ") {\n";
    out << "    }\n";
  }
  out << "}\n";

  if (!local_scopes.empty()) {
    const auto root_children = DirectChildScopes(local_scopes, "");
    if (!root_children.empty()) {
      const auto it_first = local_scopes.find(root_children.front());
      if (it_first != local_scopes.end() && it_first->second.enums.empty()) {
        out << "\n";
      }
    }
    for (const auto& child : root_children) {
      EmitNestedClassSource(&out, spec.name, child, local_scopes, user_types);
    }
  }

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
  const auto user_types = BuildUserTypeMap(spec);
  const auto local_scopes = DecodeEmbeddedScopes(spec);
  const auto ruby_indent = [](int level) { return std::string(level * 2, ' '); };
  const auto ruby_scope_path = [&](const std::string& scope_name) {
    std::ostringstream out;
    const auto parts = SplitScopePath(scope_name);
    for (size_t i = 0; i < parts.size(); i++) {
      if (i > 0) out << "::";
      out << ToUpperCamelIdentifier(parts[i]);
    }
    return out.str();
  };

  auto ruby_scope_ref = [&](const std::string& current_scope, const std::string& target_scope) {
    if (target_scope == current_scope) return ToUpperCamelIdentifier(LastScopeSegment(target_scope));
    if (current_scope.empty()) {
      const std::string rel = ruby_scope_path(target_scope);
      if (!rel.empty()) return rel;
    } else {
      const std::string prefix = current_scope + "::";
      if (target_scope.rfind(prefix, 0) == 0) {
        const std::string rel = target_scope.substr(prefix.size());
        if (!rel.empty()) return ruby_scope_path(rel);
      }
    }
    const std::string rooted = ruby_scope_path(target_scope);
    return rooted.empty() ? class_name : class_name + "::" + rooted;
  };

  auto ruby_user_type_ref = [&](const std::string& current_scope, const ir::TypeRef& type_ref) {
    const auto resolved = ResolveScopeRef(type_ref.user_type, spec.name, local_scopes);
    if (resolved.has_value()) return ruby_scope_ref(current_scope, *resolved);
    const auto parts = SplitScopePath(type_ref.user_type);
    if (parts.empty()) return ToUpperCamelIdentifier(type_ref.user_type);
    std::ostringstream out;
    for (size_t i = 0; i < parts.size(); i++) {
      if (i > 0) out << "::";
      out << ToUpperCamelIdentifier(parts[i]);
    }
    return out.str();
  };

  std::function<std::string(const ir::Expr&, int, const std::set<std::string>&,
                            const std::set<std::string>&, bool, const std::string&)> expr;
  expr = [&](const ir::Expr& e, int parent_prec, const std::set<std::string>& attrs,
             const std::set<std::string>& known_instances, bool use_reader_names,
             const std::string& repeat_item) {
    switch (e.kind) {
    case ir::Expr::Kind::kInt:
      return std::to_string(e.int_value);
    case ir::Expr::Kind::kBool:
      return e.bool_value ? std::string("true") : std::string("false");
    case ir::Expr::Kind::kName:
      if (!repeat_item.empty() && e.text == "_") return repeat_item;
      if (attrs.find(e.text) != attrs.end() || known_instances.find(e.text) != known_instances.end()) {
        return use_reader_names ? e.text : ("@" + e.text);
      }
      return e.text;
    case ir::Expr::Kind::kUnary: {
      std::string payload;
      if (ParseSpecialUnary(e.text, "__cast__:", &payload)) {
        return expr(*e.lhs, 90, attrs, known_instances, use_reader_names, repeat_item);
      }
      if (ParseSpecialUnary(e.text, "__attr__:", &payload)) {
        return expr(*e.lhs, 90, attrs, known_instances, use_reader_names, repeat_item) + "." + payload;
      }
      return "(" + NormalizeOp(e.text) + expr(*e.lhs, 90, attrs, known_instances, use_reader_names, repeat_item) + ")";
    }
    case ir::Expr::Kind::kBinary: {
      const int prec = ExprPrecedence(e);
      std::string rendered = expr(*e.lhs, prec, attrs, known_instances, use_reader_names, repeat_item) +
                             " " + NormalizeOp(e.text) + " " +
                             expr(*e.rhs, prec + 1, attrs, known_instances, use_reader_names, repeat_item);
      if (prec <= parent_prec) rendered = "(" + rendered + ")";
      return rendered;
    }
    }
    return std::string("0");
  };

  auto read_primitive = [&](ir::PrimitiveType primitive, std::optional<ir::Endian> override_endian,
                            ir::Endian default_endian) {
    if (primitive == ir::PrimitiveType::kBytes) return std::string("@_io.read_bytes_full");
    if (primitive == ir::PrimitiveType::kStr) return std::string("''");
    return std::string("@_io.") + ReadMethod(primitive, override_endian.value_or(default_endian));
  };

  std::ostringstream out;
  out << "# This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild\n";
  out << "\n";
  out << "require 'kaitai/struct/struct'\n\n";
  out << "unless Gem::Version.new(Kaitai::Struct::VERSION) >= Gem::Version.new('0.11')\n";
  out << "  raise \"Incompatible Kaitai Struct Ruby API: 0.11 or later is required, but you have #{Kaitai::Struct::VERSION}\"\n";
  out << "end\n\n";

  std::function<void(const ir::Spec&, const std::string&, const std::string&, int, bool)> emit_class;
  emit_class = [&](const ir::Spec& scope_spec, const std::string& scope_name,
                   const std::string& ruby_name, int indent, bool is_root) {
    const std::string ind = ruby_indent(indent);
    const std::string ind1 = ruby_indent(indent + 1);
    const std::string ind2 = ruby_indent(indent + 2);
    std::set<std::string> attrs;
    for (const auto& a : scope_spec.attrs) attrs.insert(a.id);
    std::set<std::string> known_instances;

    auto read_value = [&](const ir::Attr& attr) {
      const auto primitive = ResolvePrimitiveType(attr.type, user_types).value_or(ir::PrimitiveType::kU1);
      const bool unresolved_user =
          IsUnresolvedUserType(attr.type, user_types) && !attr.switch_on.has_value();
      if (unresolved_user) {
        return ruby_user_type_ref(scope_name, attr.type) + ".new(@_io, self, @_root)";
      }
      if (primitive == ir::PrimitiveType::kBytes) {
        std::string read =
            attr.size_expr.has_value()
                ? ("@_io.read_bytes(" +
                   expr(*attr.size_expr, -1, attrs, known_instances, true, "") + ")")
                : "@_io.read_bytes_full";
        if (attr.process.has_value() &&
            attr.process->kind == ir::Attr::Process::Kind::kXorConst) {
          read = "Kaitai::Struct::Stream.process_xor_one(" + read + ", " +
                 std::to_string(attr.process->xor_const) + ")";
        }
        return read;
      }
      if (primitive == ir::PrimitiveType::kStr && attr.size_expr.has_value()) {
        return "(@_io.read_bytes(" +
               expr(*attr.size_expr, -1, attrs, known_instances, true, "") +
               ")).force_encoding(\"" + attr.encoding.value_or("UTF-8") +
               "\").encode('UTF-8')";
      }
      return read_primitive(primitive, attr.endian_override, scope_spec.default_endian);
    };

    auto read_parse_instance = [&](const ir::Instance& inst) {
      const auto primitive = ResolvePrimitiveType(inst.type, user_types).value_or(ir::PrimitiveType::kU1);
      if (IsUnresolvedUserType(inst.type, user_types)) {
        return ruby_user_type_ref(scope_name, inst.type) + ".new(@_io, self, @_root)";
      }
      if (primitive == ir::PrimitiveType::kBytes) {
        if (inst.size_expr.has_value()) {
          return "@_io.read_bytes(" +
                 expr(*inst.size_expr, -1, attrs, known_instances, true, "") + ")";
        }
        return std::string("@_io.read_bytes_full");
      }
      if (primitive == ir::PrimitiveType::kStr) {
        if (!inst.size_expr.has_value()) return std::string("\"\"");
        return "(@_io.read_bytes(" +
               expr(*inst.size_expr, -1, attrs, known_instances, true, "") +
               ")).force_encoding(\"" + inst.encoding.value_or("UTF-8") +
               "\").encode('UTF-8')";
      }
      return "@_io." + ReadMethod(primitive, inst.endian_override.value_or(scope_spec.default_endian));
    };

    out << ind << "class " << ruby_name << " < Kaitai::Struct::Struct\n";
    out << ind1 << "def initialize(_io, _parent = nil, _root = nil)\n";
    if (is_root) {
      out << ind2 << "super(_io, _parent, _root || self)\n";
    } else {
      out << ind2 << "super(_io, _parent, _root)\n";
    }
    out << ind2 << "_read\n";
    out << ind1 << "end\n\n";
    out << ind1 << "def _read\n";
    for (const auto& attr : scope_spec.attrs) {
      if (attr.repeat == ir::Attr::RepeatKind::kExpr) {
        out << ind2 << "@" << attr.id << " = []\n";
        out << ind2 << "("
            << expr(*attr.repeat_expr, -1, attrs, known_instances, true, "") << ").times { |i|\n";
        out << ind2 << "  @" << attr.id << " << " << read_value(attr) << "\n";
        out << ind2 << "}\n";
        continue;
      }
      if (attr.repeat == ir::Attr::RepeatKind::kEos) {
        out << ind2 << "@" << attr.id << " = []\n";
        out << ind2 << "i = 0\n";
        out << ind2 << "while not @_io.eof?\n";
        out << ind2 << "  @" << attr.id << " << " << read_value(attr) << "\n";
        out << ind2 << "  i += 1\n";
        out << ind2 << "end\n";
        continue;
      }
      if (attr.repeat == ir::Attr::RepeatKind::kUntil) {
        out << ind2 << "@" << attr.id << " = []\n";
        out << ind2 << "i = 0\n";
        out << ind2 << "loop do\n";
        out << ind2 << "  _ = " << read_value(attr) << "\n";
        out << ind2 << "  @" << attr.id << " << _\n";
        out << ind2 << "  i += 1\n";
        out << ind2 << "  break if "
            << expr(*attr.repeat_expr, -1, attrs, known_instances, true, "_") << "\n";
        out << ind2 << "end\n";
        continue;
      }
      if (attr.switch_on.has_value()) {
        out << ind2 << "case "
            << expr(*attr.switch_on, -1, attrs, known_instances, true, "") << "\n";
        for (const auto& c : attr.switch_cases) {
          if (c.match_expr.has_value()) {
            out << ind2 << "when "
                << expr(*c.match_expr, -1, attrs, known_instances, true, "") << "\n";
          } else {
            out << ind2 << "else\n";
          }
          const bool case_user = IsUnresolvedUserType(c.type, user_types);
          if (case_user) {
            out << ind2 << "  @" << attr.id << " = "
                << ruby_user_type_ref(scope_name, c.type)
                << ".new(@_io, self, @_root)\n";
          } else {
            const auto case_primitive =
                ResolvePrimitiveType(c.type, user_types).value_or(ir::PrimitiveType::kU1);
            out << ind2 << "  @" << attr.id << " = "
                << read_primitive(case_primitive, attr.endian_override, scope_spec.default_endian) << "\n";
          }
        }
        out << ind2 << "end\n";
        continue;
      }
      out << ind2 << "@" << attr.id << " = " << read_value(attr) << "\n";
    }
    for (const auto& v : scope_spec.validations) {
      out << ind2
          << "raise Kaitai::Struct::ValidationExprError.new(@" << v.target
          << ", @_io, '/valid/" << v.target << "') if !("
          << expr(v.condition_expr, -1, attrs, known_instances, true, "") << ")\n";
    }
    out << ind2 << "self\n";
    out << ind1 << "end\n";

    for (const auto& child : DirectChildScopes(local_scopes, scope_name)) {
      const auto it_child = local_scopes.find(child);
      if (it_child != local_scopes.end()) {
        emit_class(it_child->second, child, ToUpperCamelIdentifier(LastScopeSegment(child)),
                   indent + 1, false);
      }
    }

    for (const auto& attr : scope_spec.attrs) {
      out << ind1 << "attr_reader :" << attr.id << "\n";
    }
    bool first_instance = true;
    for (const auto& inst : scope_spec.instances) {
      if (!first_instance) out << "\n";
      out << ind1 << "def " << inst.id << "\n";
      out << ind2 << "return @" << inst.id << " unless @" << inst.id << ".nil?\n";
      if (inst.kind == ir::Instance::Kind::kParse) {
        out << ind2 << "_pos = @_io.pos\n";
        if (inst.pos_expr.has_value()) {
          out << ind2 << "@_io.seek("
              << expr(*inst.pos_expr, -1, attrs, known_instances, true, "") << ")\n";
        }
        out << ind2 << "@" << inst.id << " = " << read_parse_instance(inst) << "\n";
        out << ind2 << "@_io.seek(_pos)\n";
      } else {
        out << ind2 << "@" << inst.id
            << " = " << expr(inst.value_expr, -1, attrs, known_instances, true, "") << "\n";
      }
      out << ind2 << "@" << inst.id << "\n" << ind1 << "end\n";
      known_instances.insert(inst.id);
      first_instance = false;
    }
    out << ind << "end\n";
  };

  emit_class(spec, "", class_name, 0, true);
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
