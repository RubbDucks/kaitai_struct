#include "ir.h"

#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kscpp::ir {
namespace {

std::string EndianToString(Endian endian) { return endian == Endian::kLe ? "le" : "be"; }

ValidationResult EndianFromString(const std::string& text, Endian* out) {
  if (text == "le") {
    *out = Endian::kLe;
    return {true, ""};
  }
  if (text == "be") {
    *out = Endian::kBe;
    return {true, ""};
  }
  return {false, "invalid endian: " + text};
}

std::string PrimitiveToString(PrimitiveType t) {
  switch (t) {
  case PrimitiveType::kU1:
    return "u1";
  case PrimitiveType::kU2:
    return "u2";
  case PrimitiveType::kU4:
    return "u4";
  case PrimitiveType::kU8:
    return "u8";
  case PrimitiveType::kS1:
    return "s1";
  case PrimitiveType::kS2:
    return "s2";
  case PrimitiveType::kS4:
    return "s4";
  case PrimitiveType::kS8:
    return "s8";
  case PrimitiveType::kF4:
    return "f4";
  case PrimitiveType::kF8:
    return "f8";
  case PrimitiveType::kStr:
    return "str";
  case PrimitiveType::kBytes:
    return "bytes";
  }
  return "u1";
}

ValidationResult PrimitiveFromString(const std::string& text, PrimitiveType* out) {
  if (text == "u1")
    *out = PrimitiveType::kU1;
  else if (text == "u2")
    *out = PrimitiveType::kU2;
  else if (text == "u4")
    *out = PrimitiveType::kU4;
  else if (text == "u8")
    *out = PrimitiveType::kU8;
  else if (text == "s1")
    *out = PrimitiveType::kS1;
  else if (text == "s2")
    *out = PrimitiveType::kS2;
  else if (text == "s4")
    *out = PrimitiveType::kS4;
  else if (text == "s8")
    *out = PrimitiveType::kS8;
  else if (text == "f4")
    *out = PrimitiveType::kF4;
  else if (text == "f8")
    *out = PrimitiveType::kF8;
  else if (text == "str")
    *out = PrimitiveType::kStr;
  else if (text == "bytes")
    *out = PrimitiveType::kBytes;
  else
    return {false, "invalid primitive type: " + text};
  return {true, ""};
}

std::string SerializeExpr(const Expr& expr) {
  std::ostringstream out;
  switch (expr.kind) {
  case Expr::Kind::kInt:
    out << "(int " << expr.int_value << ")";
    return out.str();
  case Expr::Kind::kBool:
    out << "(bool " << (expr.bool_value ? "true" : "false") << ")";
    return out.str();
  case Expr::Kind::kName:
    out << "(name " << std::quoted(expr.text) << ")";
    return out.str();
  case Expr::Kind::kUnary:
    out << "(un " << std::quoted(expr.text) << " " << SerializeExpr(*expr.lhs) << ")";
    return out.str();
  case Expr::Kind::kBinary:
    out << "(bin " << std::quoted(expr.text) << " " << SerializeExpr(*expr.lhs) << " "
        << SerializeExpr(*expr.rhs) << ")";
    return out.str();
  }
  return "";
}

struct ExprParser {
  std::string input;
  size_t pos = 0;

  char Peek() const { return pos < input.size() ? input[pos] : '\0'; }
  char Next() { return pos < input.size() ? input[pos++] : '\0'; }

  void SkipWs() {
    while (Peek() == ' ' || Peek() == '\n' || Peek() == '\t' || Peek() == '\r') {
      Next();
    }
  }

  bool ReadToken(std::string* out) {
    SkipWs();
    out->clear();
    while (true) {
      char c = Peek();
      if (c == '\0' || c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '(' || c == ')') {
        break;
      }
      out->push_back(Next());
    }
    return !out->empty();
  }

  bool ReadQuoted(std::string* out) {
    SkipWs();
    if (Next() != '"')
      return false;
    std::ostringstream q;
    q << '"';
    bool escaped = false;
    while (pos <= input.size()) {
      char c = Next();
      if (c == '\0')
        return false;
      q << c;
      if (escaped) {
        escaped = false;
        continue;
      }
      if (c == '\\') {
        escaped = true;
        continue;
      }
      if (c == '"') {
        break;
      }
    }
    std::istringstream parse(q.str());
    parse >> std::quoted(*out);
    return !parse.fail();
  }

  ValidationResult Parse(Expr* out) {
    SkipWs();
    if (Next() != '(')
      return {false, "expression must start with '('"};
    std::string tag;
    if (!ReadToken(&tag))
      return {false, "missing expression tag"};
    if (tag == "int") {
      std::string num;
      if (!ReadToken(&num))
        return {false, "missing int literal"};
      out->kind = Expr::Kind::kInt;
      out->int_value = std::stoll(num);
    } else if (tag == "bool") {
      std::string val;
      if (!ReadToken(&val))
        return {false, "missing bool literal"};
      out->kind = Expr::Kind::kBool;
      out->bool_value = val == "true";
      if (val != "true" && val != "false")
        return {false, "invalid bool literal: " + val};
    } else if (tag == "name") {
      std::string val;
      if (!ReadQuoted(&val))
        return {false, "invalid name literal"};
      out->kind = Expr::Kind::kName;
      out->text = val;
    } else if (tag == "un") {
      std::string op;
      if (!ReadQuoted(&op))
        return {false, "invalid unary operator"};
      Expr operand;
      auto res = Parse(&operand);
      if (!res.ok)
        return res;
      *out = Expr::Unary(op, std::move(operand));
    } else if (tag == "bin") {
      std::string op;
      if (!ReadQuoted(&op))
        return {false, "invalid binary operator"};
      Expr left;
      auto left_res = Parse(&left);
      if (!left_res.ok)
        return left_res;
      Expr right;
      auto right_res = Parse(&right);
      if (!right_res.ok)
        return right_res;
      *out = Expr::Binary(op, std::move(left), std::move(right));
    } else {
      return {false, "unknown expression tag: " + tag};
    }
    SkipWs();
    if (Next() != ')')
      return {false, "expression missing closing ')'"};
    return {true, ""};
  }
};

std::string SerializeTypeRef(const TypeRef& t) {
  std::ostringstream out;
  if (t.kind == TypeRef::Kind::kPrimitive) {
    out << "primitive " << std::quoted(PrimitiveToString(t.primitive));
  } else {
    out << "user " << std::quoted(t.user_type);
  }
  return out.str();
}

ValidationResult ParseTypeRef(std::istringstream* in, TypeRef* out) {
  std::string kind;
  std::string payload;
  if (!(*in >> kind >> std::quoted(payload))) {
    return {false, "invalid type reference"};
  }
  if (kind == "primitive") {
    out->kind = TypeRef::Kind::kPrimitive;
    return PrimitiveFromString(payload, &out->primitive);
  }
  if (kind == "user") {
    out->kind = TypeRef::Kind::kUser;
    out->user_type = payload;
    return {true, ""};
  }
  return {false, "unknown type reference kind: " + kind};
}

} // namespace

Expr Expr::Int(long long value) {
  Expr expr;
  expr.kind = Kind::kInt;
  expr.int_value = value;
  return expr;
}

Expr Expr::Bool(bool value) {
  Expr expr;
  expr.kind = Kind::kBool;
  expr.bool_value = value;
  return expr;
}

Expr Expr::Name(std::string value) {
  Expr expr;
  expr.kind = Kind::kName;
  expr.text = std::move(value);
  return expr;
}

Expr Expr::Unary(std::string op, Expr value) {
  Expr expr;
  expr.kind = Kind::kUnary;
  expr.text = std::move(op);
  expr.lhs = std::make_shared<Expr>(std::move(value));
  return expr;
}

Expr Expr::Binary(std::string op, Expr left, Expr right) {
  Expr expr;
  expr.kind = Kind::kBinary;
  expr.text = std::move(op);
  expr.lhs = std::make_shared<Expr>(std::move(left));
  expr.rhs = std::make_shared<Expr>(std::move(right));
  return expr;
}

ValidationResult Validate(const Spec& spec) {
  if (spec.name.empty()) {
    return {false, "spec.name is required"};
  }

  std::unordered_set<std::string> declared_types;
  declared_types.insert(spec.name);
  std::unordered_map<std::string, std::string> type_alias_edges;

  for (const auto& t : spec.types) {
    if (t.name.empty()) {
      return {false, "type.name is required"};
    }
    if (!declared_types.insert(t.name).second) {
      return {false, "duplicate type declaration: " + t.name};
    }
    if (t.type.kind == TypeRef::Kind::kUser) {
      if (t.type.user_type.empty()) {
        return {false, "user type reference requires user_type"};
      }
      type_alias_edges[t.name] = t.type.user_type;
    }
  }

  auto require_known_type = [&](const TypeRef& ref,
                                const std::string& context) -> ValidationResult {
    if (ref.kind != TypeRef::Kind::kUser) {
      return {true, ""};
    }
    if (ref.user_type.empty()) {
      return {false, context + " user type reference requires user_type"};
    }
    if (declared_types.find(ref.user_type) == declared_types.end()) {
      return {false, context + " references unknown user type: " + ref.user_type};
    }
    return {true, ""};
  };

  for (const auto& attr : spec.attrs) {
    if (attr.id.empty()) {
      return {false, "attr.id is required"};
    }
    auto type_check = require_known_type(attr.type, "attr");
    if (!type_check.ok)
      return type_check;
  }

  for (const auto& inst : spec.instances) {
    if (inst.id.empty()) {
      return {false, "instance.id is required"};
    }
  }

  for (const auto& val : spec.validations) {
    if (val.target.empty()) {
      return {false, "validation.target is required"};
    }
  }

  enum class VisitState { kNotVisited, kVisiting, kVisited };
  std::unordered_map<std::string, VisitState> visit_states;
  for (const auto& edge : type_alias_edges) {
    visit_states[edge.first] = VisitState::kNotVisited;
  }

  std::function<ValidationResult(const std::string&)> visit =
      [&](const std::string& name) -> ValidationResult {
    auto state_it = visit_states.find(name);
    if (state_it == visit_states.end()) {
      return {true, ""};
    }
    if (state_it->second == VisitState::kVisiting) {
      return {false, "type alias cycle detected at: " + name};
    }
    if (state_it->second == VisitState::kVisited) {
      return {true, ""};
    }
    state_it->second = VisitState::kVisiting;

    const auto edge = type_alias_edges.find(name);
    if (edge != type_alias_edges.end()) {
      const std::string& target = edge->second;
      if (declared_types.find(target) == declared_types.end()) {
        return {false,
                std::string("type \"") + name + "\" references unknown user type: " + target};
      }
      auto r = visit(target);
      if (!r.ok)
        return r;
    }

    state_it->second = VisitState::kVisited;
    return {true, ""};
  };

  for (const auto& edge : type_alias_edges) {
    auto r = visit(edge.first);
    if (!r.ok)
      return r;
  }

  return {true, ""};
}

std::string Serialize(const Spec& spec) {
  std::ostringstream out;
  out << "KSIR1\n";
  out << "name " << std::quoted(spec.name) << "\n";
  out << "default_endian " << EndianToString(spec.default_endian) << "\n";

  out << "types " << spec.types.size() << "\n";
  for (const auto& t : spec.types) {
    out << "type " << std::quoted(t.name) << " " << SerializeTypeRef(t.type) << "\n";
  }

  out << "attrs " << spec.attrs.size() << "\n";
  for (const auto& a : spec.attrs) {
    out << "attr " << std::quoted(a.id) << " " << SerializeTypeRef(a.type) << " ";
    if (a.endian_override.has_value()) {
      out << EndianToString(*a.endian_override);
    } else {
      out << "none";
    }
    out << " ";
    if (a.size_expr.has_value()) {
      out << std::quoted(SerializeExpr(*a.size_expr));
    } else {
      out << std::quoted(std::string("none"));
    }
    out << "\n";
  }

  out << "instances " << spec.instances.size() << "\n";
  for (const auto& i : spec.instances) {
    out << "instance " << std::quoted(i.id) << " " << std::quoted(SerializeExpr(i.value_expr))
        << "\n";
  }

  out << "validations " << spec.validations.size() << "\n";
  for (const auto& v : spec.validations) {
    out << "validation " << std::quoted(v.target) << " "
        << std::quoted(SerializeExpr(v.condition_expr)) << " " << std::quoted(v.message) << "\n";
  }
  out << "end\n";

  return out.str();
}

ValidationResult Deserialize(const std::string& encoded, Spec* out) {
  std::istringstream in(encoded);
  std::string line;

  if (!std::getline(in, line) || line != "KSIR1") {
    return {false, "missing KSIR1 header"};
  }

  if (!std::getline(in, line))
    return {false, "missing spec name line"};
  {
    std::istringstream row(line);
    std::string key;
    if (!(row >> key) || key != "name" || !(row >> std::quoted(out->name))) {
      return {false, "invalid name line"};
    }
  }

  if (!std::getline(in, line))
    return {false, "missing default endian line"};
  {
    std::istringstream row(line);
    std::string key;
    std::string endian_text;
    if (!(row >> key >> endian_text) || key != "default_endian") {
      return {false, "invalid default_endian line"};
    }
    auto r = EndianFromString(endian_text, &out->default_endian);
    if (!r.ok)
      return r;
  }

  auto parse_count = [&](const std::string& expected, size_t* count) -> ValidationResult {
    if (!std::getline(in, line))
      return {false, "missing section header: " + expected};
    std::istringstream row(line);
    std::string key;
    if (!(row >> key >> *count) || key != expected) {
      return {false, "invalid section header: " + expected};
    }
    return {true, ""};
  };

  size_t count = 0;
  auto res = parse_count("types", &count);
  if (!res.ok)
    return res;
  out->types.clear();
  for (size_t i = 0; i < count; i++) {
    if (!std::getline(in, line))
      return {false, "truncated type section"};
    std::istringstream row(line);
    std::string key;
    TypeDef type;
    if (!(row >> key >> std::quoted(type.name)) || key != "type") {
      return {false, "invalid type row"};
    }
    auto p = ParseTypeRef(&row, &type.type);
    if (!p.ok)
      return p;
    out->types.push_back(type);
  }

  res = parse_count("attrs", &count);
  if (!res.ok)
    return res;
  out->attrs.clear();
  for (size_t i = 0; i < count; i++) {
    if (!std::getline(in, line))
      return {false, "truncated attr section"};
    std::istringstream row(line);
    std::string key;
    Attr attr;
    std::string endian_text;
    std::string size_expr_text;

    if (!(row >> key >> std::quoted(attr.id)) || key != "attr") {
      return {false, "invalid attr row"};
    }
    auto p = ParseTypeRef(&row, &attr.type);
    if (!p.ok)
      return p;
    if (!(row >> endian_text >> std::quoted(size_expr_text))) {
      return {false, "invalid attr row suffix"};
    }
    if (endian_text != "none") {
      Endian parsed;
      auto e = EndianFromString(endian_text, &parsed);
      if (!e.ok)
        return e;
      attr.endian_override = parsed;
    }
    if (size_expr_text != "none") {
      ExprParser parser{size_expr_text};
      Expr expr;
      auto pe = parser.Parse(&expr);
      if (!pe.ok)
        return pe;
      attr.size_expr = expr;
    }
    out->attrs.push_back(attr);
  }

  res = parse_count("instances", &count);
  if (!res.ok)
    return res;
  out->instances.clear();
  for (size_t i = 0; i < count; i++) {
    if (!std::getline(in, line))
      return {false, "truncated instance section"};
    std::istringstream row(line);
    std::string key;
    Instance inst;
    std::string expr_text;
    if (!(row >> key >> std::quoted(inst.id) >> std::quoted(expr_text)) || key != "instance") {
      return {false, "invalid instance row"};
    }
    ExprParser parser{expr_text};
    auto pe = parser.Parse(&inst.value_expr);
    if (!pe.ok)
      return pe;
    out->instances.push_back(inst);
  }

  res = parse_count("validations", &count);
  if (!res.ok)
    return res;
  out->validations.clear();
  for (size_t i = 0; i < count; i++) {
    if (!std::getline(in, line))
      return {false, "truncated validation section"};
    std::istringstream row(line);
    std::string key;
    Validation val;
    std::string expr_text;
    if (!(row >> key >> std::quoted(val.target) >> std::quoted(expr_text) >>
          std::quoted(val.message)) ||
        key != "validation") {
      return {false, "invalid validation row"};
    }
    ExprParser parser{expr_text};
    auto pe = parser.Parse(&val.condition_expr);
    if (!pe.ok)
      return pe;
    out->validations.push_back(val);
  }

  if (!std::getline(in, line) || line != "end") {
    return {false, "missing end marker"};
  }

  return Validate(*out);
}

ValidationResult LoadFromFile(const std::string& path, Spec* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {false, "failed to open IR file: " + path};
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return Deserialize(buffer.str(), out);
}

} // namespace kscpp::ir
