#include "ir.h"

#include <fstream>
#include <filesystem>
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



std::string RepeatKindToString(const Attr::RepeatKind repeat) {
  switch (repeat) {
  case Attr::RepeatKind::kNone:
    return "none";
  case Attr::RepeatKind::kEos:
    return "eos";
  case Attr::RepeatKind::kExpr:
    return "expr";
  case Attr::RepeatKind::kUntil:
    return "until";
  }
  return "none";
}

ValidationResult RepeatKindFromString(const std::string& text, Attr::RepeatKind* out) {
  if (text == "none") {
    *out = Attr::RepeatKind::kNone;
    return {true, ""};
  }
  if (text == "eos") {
    *out = Attr::RepeatKind::kEos;
    return {true, ""};
  }
  if (text == "expr") {
    *out = Attr::RepeatKind::kExpr;
    return {true, ""};
  }
  if (text == "until") {
    *out = Attr::RepeatKind::kUntil;
    return {true, ""};
  }
  return {false, "invalid repeat kind: " + text};
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

std::string NormalizeImportPath(const std::string& import_name) {
  std::string out = import_name;
  for (char& c : out) {
    if (c == '\\') c = '/';
  }
  return out;
}

ValidationResult ResolveImportPath(const std::string& import_name,
                                   const std::filesystem::path& current_file,
                                   const std::vector<std::string>& import_paths,
                                   std::filesystem::path* resolved) {
  const std::filesystem::path normalized(NormalizeImportPath(import_name));
  std::vector<std::filesystem::path> candidates;
  if (normalized.is_absolute()) {
    candidates.push_back(normalized);
  } else {
    candidates.push_back(current_file.parent_path() / normalized);
    for (const auto& base : import_paths) {
      if (!base.empty()) {
        candidates.push_back(std::filesystem::path(base) / normalized);
      }
    }
  }

  for (const auto& candidate : candidates) {
    std::error_code ec;
    auto canon = std::filesystem::weakly_canonical(candidate, ec);
    if (!ec && std::filesystem::exists(canon)) {
      *resolved = canon;
      return {true, ""};
    }
  }
  return {false, "failed to resolve import: " + import_name + " from " + current_file.string()};
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

  std::unordered_set<std::string> enum_names;
  for (const auto& e : spec.enums) {
    if (e.name.empty()) {
      return {false, "enum.name is required"};
    }
    if (!enum_names.insert(e.name).second) {
      return {false, "duplicate enum declaration: " + e.name};
    }
    if (e.values.empty()) {
      return {false, "enum.values must not be empty: " + e.name};
    }
    std::unordered_set<std::string> value_names;
    for (const auto& value : e.values) {
      if (value.name.empty()) {
        return {false, "enum value name is required in enum: " + e.name};
      }
      if (!value_names.insert(value.name).second) {
        return {false, "duplicate enum value name in enum " + e.name + ": " + value.name};
      }
    }
  }

  for (const auto& attr : spec.attrs) {
    if (attr.id.empty()) {
      return {false, "attr.id is required"};
    }
    auto type_check = require_known_type(attr.type, "attr");
    if (!type_check.ok)
      return type_check;
    if (attr.encoding.has_value() && attr.type.kind == TypeRef::Kind::kPrimitive && attr.type.primitive != PrimitiveType::kStr) {
      return {false, "attr.encoding is only allowed for primitive str type"};
    }
    if (attr.repeat == Attr::RepeatKind::kExpr && !attr.repeat_expr.has_value()) {
      return {false, "attr.repeat_expr is required when repeat=expr"};
    }
    if (attr.repeat == Attr::RepeatKind::kUntil && !attr.repeat_expr.has_value()) {
      return {false, "attr.repeat_expr is required when repeat=until"};
    }
    if ((attr.repeat == Attr::RepeatKind::kNone || attr.repeat == Attr::RepeatKind::kEos) &&
        attr.repeat_expr.has_value()) {
      return {false, "attr.repeat_expr is only allowed when repeat=expr/until"};
    }
    if (!attr.switch_cases.empty() && !attr.switch_on.has_value()) {
      return {false, "attr.switch_cases requires attr.switch_on"};
    }
    if (attr.switch_on.has_value() && attr.switch_cases.empty()) {
      return {false, "attr.switch_on requires at least one switch case"};
    }
    bool has_switch_else = false;
    for (const auto& c : attr.switch_cases) {
      if (!c.match_expr.has_value()) {
        if (has_switch_else) {
          return {false, "attr.switch_cases has duplicate switch else case"};
        }
        has_switch_else = true;
      }
      if (c.type.kind != TypeRef::Kind::kPrimitive) {
        return {false, "switch case user-defined types are not supported in this migration slice"};
      }
    }
    if (attr.enum_name.has_value()) {
      if (attr.type.kind != TypeRef::Kind::kPrimitive) {
        return {false, "attr.enum_name requires primitive integer type"};
      }
      switch (attr.type.primitive) {
      case PrimitiveType::kU1:
      case PrimitiveType::kU2:
      case PrimitiveType::kU4:
      case PrimitiveType::kU8:
      case PrimitiveType::kS1:
      case PrimitiveType::kS2:
      case PrimitiveType::kS4:
      case PrimitiveType::kS8:
        break;
      default:
        return {false, "attr.enum_name requires primitive integer type"};
      }
      bool enum_found = enum_names.find(*attr.enum_name) != enum_names.end();
      if (!enum_found) {
        for (const auto& enum_name : enum_names) {
          if (enum_name.size() > attr.enum_name->size() &&
              enum_name.compare(enum_name.size() - attr.enum_name->size(), attr.enum_name->size(), *attr.enum_name) == 0 &&
              enum_name[enum_name.size() - attr.enum_name->size() - 1] == ':') {
            enum_found = true;
            break;
          }
        }
      }
      if (!enum_found) {
        return {false, "attr references unknown enum: " + *attr.enum_name};
      }
    }
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
  out << "imports " << spec.imports.size() << "\n";
  for (const auto& imp : spec.imports) {
    out << "import " << std::quoted(imp) << "\n";
  }

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
    out << " " << std::quoted(a.enum_name.value_or("none"));
    out << " " << std::quoted(a.encoding.value_or("none"));
    out << "\n";
  }

  out << "enums " << spec.enums.size() << "\n";
  for (const auto& e : spec.enums) {
    out << "enum " << std::quoted(e.name) << " " << e.values.size() << "\n";
    for (const auto& v : e.values) {
      out << "enum_value " << v.value << " " << std::quoted(v.name) << "\n";
    }
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

ValidationResult Deserialize(const std::string& encoded, Spec* out, bool validate) {
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
  if (!std::getline(in, line))
    return {false, "missing section header: imports/types"};
  out->imports.clear();
  {
    std::istringstream row(line);
    std::string key;
    size_t section_count = 0;
    if (!(row >> key >> section_count)) {
      return {false, "invalid section header: imports/types"};
    }
    if (key == "imports") {
      count = section_count;
      for (size_t i = 0; i < count; i++) {
        if (!std::getline(in, line))
          return {false, "truncated import section"};
        std::istringstream import_row(line);
        std::string import_key;
        std::string import_name;
        if (!(import_row >> import_key >> std::quoted(import_name)) || import_key != "import") {
          return {false, "invalid import row"};
        }
        out->imports.push_back(import_name);
      }
      auto imports_res = parse_count("types", &count);
      if (!imports_res.ok)
        return imports_res;
    } else if (key == "types") {
      count = section_count;
    } else {
      return {false, "invalid section header: imports/types"};
    }
  }

  ValidationResult res{true, ""};
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
    std::string enum_name_text;
    std::string encoding_text;

    if (!(row >> key >> std::quoted(attr.id)) || key != "attr") {
      return {false, "invalid attr row"};
    }
    auto p = ParseTypeRef(&row, &attr.type);
    if (!p.ok)
      return p;
    if (!(row >> endian_text >> std::quoted(size_expr_text))) {
      return {false, "invalid attr row suffix"};
    }
    if (!(row >> std::quoted(enum_name_text) >> std::quoted(encoding_text))) {
      enum_name_text = "none";
      encoding_text = "none";
    }
    std::string if_expr_text = "none";
    std::string repeat_kind_text = "none";
    std::string repeat_expr_text = "none";
    std::string switch_on_text = "none";
    size_t switch_case_count = 0;
    if (row >> std::quoted(if_expr_text) >> repeat_kind_text >> std::quoted(repeat_expr_text) >>
            std::quoted(switch_on_text) >> switch_case_count) {
      for (size_t sc = 0; sc < switch_case_count; sc++) {
        Attr::SwitchCase cs;
        std::string match_expr_text;
        if (!(row >> std::quoted(match_expr_text))) {
          return {false, "invalid switch case row"};
        }
        auto cp = ParseTypeRef(&row, &cs.type);
        if (!cp.ok) {
          return cp;
        }
        if (match_expr_text != "else") {
          ExprParser match_parser{match_expr_text};
          Expr parsed_match;
          auto mp = match_parser.Parse(&parsed_match);
          if (!mp.ok) {
            return mp;
          }
          cs.match_expr = parsed_match;
        }
        attr.switch_cases.push_back(cs);
      }
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
    if (enum_name_text != "none") {
      attr.enum_name = enum_name_text;
    }
    if (encoding_text != "none") {
      attr.encoding = encoding_text;
    }
    if (if_expr_text != "none") {
      ExprParser if_parser{if_expr_text};
      Expr parsed_if;
      auto ip = if_parser.Parse(&parsed_if);
      if (!ip.ok) {
        return ip;
      }
      attr.if_expr = parsed_if;
    }
    auto repeat_parse = RepeatKindFromString(repeat_kind_text, &attr.repeat);
    if (!repeat_parse.ok) {
      return repeat_parse;
    }
    if (repeat_expr_text != "none") {
      ExprParser repeat_parser{repeat_expr_text};
      Expr parsed_repeat;
      auto rp = repeat_parser.Parse(&parsed_repeat);
      if (!rp.ok) {
        return rp;
      }
      attr.repeat_expr = parsed_repeat;
    }
    if (switch_on_text != "none") {
      ExprParser switch_parser{switch_on_text};
      Expr parsed_switch;
      auto sp = switch_parser.Parse(&parsed_switch);
      if (!sp.ok) {
        return sp;
      }
      attr.switch_on = parsed_switch;
    }
    out->attrs.push_back(attr);
  }

  res = parse_count("enums", &count);
  if (!res.ok)
    return res;
  out->enums.clear();
  for (size_t i = 0; i < count; i++) {
    if (!std::getline(in, line))
      return {false, "truncated enum section"};
    std::istringstream enum_row(line);
    std::string enum_key;
    EnumDef e;
    size_t value_count = 0;
    if (!(enum_row >> enum_key >> std::quoted(e.name) >> value_count) || enum_key != "enum") {
      return {false, "invalid enum row"};
    }
    for (size_t j = 0; j < value_count; j++) {
      if (!std::getline(in, line))
        return {false, "truncated enum value section"};
      std::istringstream vrow(line);
      std::string vkey;
      EnumValue v;
      if (!(vrow >> vkey >> v.value >> std::quoted(v.name)) || vkey != "enum_value") {
        return {false, "invalid enum value row"};
      }
      e.values.push_back(v);
    }
    out->enums.push_back(e);
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

  if (validate) return Validate(*out);
  return {true, ""};
}

ValidationResult LoadFromFile(const std::string& path, Spec* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {false, "failed to open IR file: " + path};
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return Deserialize(buffer.str(), out, true);
}

ValidationResult LoadFromFileWithImports(const std::string& path,
                                         const std::vector<std::string>& import_paths,
                                         Spec* out) {
  std::error_code ec;
  const std::filesystem::path root = std::filesystem::weakly_canonical(path, ec);
  if (ec) {
    return {false, "failed to canonicalize IR file path: " + path};
  }

  std::unordered_map<std::string, Spec> loaded;
  std::unordered_set<std::string> visiting;
  std::vector<std::string> stack;

  std::function<ValidationResult(const std::filesystem::path&)> dfs =
      [&](const std::filesystem::path& file_path) -> ValidationResult {
    const std::string file_key = file_path.string();
    if (loaded.find(file_key) != loaded.end()) {
      return {true, ""};
    }
    if (!visiting.insert(file_key).second) {
      std::string chain;
      for (const auto& n : stack) {
        if (!chain.empty()) chain += " -> ";
        chain += n;
      }
      if (!chain.empty()) chain += " -> ";
      chain += file_key;
      return {false, "import cycle detected: " + chain};
    }
    stack.push_back(file_key);

    Spec current;
    auto load = ValidationResult{false, ""};
    {
      std::ifstream in(file_key, std::ios::binary);
      if (!in) {
        stack.pop_back();
        visiting.erase(file_key);
        return {false, "failed to open IR file: " + file_key};
      }
      std::ostringstream buffer;
      buffer << in.rdbuf();
      load = Deserialize(buffer.str(), &current, false);
    }
    if (!load.ok) {
      stack.pop_back();
      visiting.erase(file_key);
      return load;
    }

    for (const auto& imp : current.imports) {
      std::filesystem::path resolved;
      auto resolve = ResolveImportPath(imp, file_path, import_paths, &resolved);
      if (!resolve.ok) {
        stack.pop_back();
        visiting.erase(file_key);
        return resolve;
      }
      auto child = dfs(resolved);
      if (!child.ok) {
        stack.pop_back();
        visiting.erase(file_key);
        return child;
      }
    }

    loaded[file_key] = current;
    stack.pop_back();
    visiting.erase(file_key);
    return {true, ""};
  };

  auto walk = dfs(root);
  if (!walk.ok) {
    return walk;
  }

  Spec merged = loaded[root.string()];
  std::unordered_set<std::string> merged_files;
  std::unordered_set<std::string> seen_type_names;
  std::unordered_set<std::string> seen_enum_names;
  seen_type_names.insert(merged.name);
  for (const auto& t : merged.types) seen_type_names.insert(t.name);
  for (const auto& e : merged.enums) seen_enum_names.insert(e.name);

  std::function<ValidationResult(const std::filesystem::path&)> merge_deps =
      [&](const std::filesystem::path& file_path) -> ValidationResult {
    const Spec& spec = loaded[file_path.string()];
    for (const auto& imp : spec.imports) {
      std::filesystem::path resolved;
      auto resolve = ResolveImportPath(imp, file_path, import_paths, &resolved);
      if (!resolve.ok) return resolve;
      const std::string dep_key = resolved.string();
      if (!merged_files.insert(dep_key).second) {
        continue;
      }
      auto nested = merge_deps(resolved);
      if (!nested.ok) return nested;

      const Spec& dep = loaded[dep_key];
      if (!seen_type_names.insert(dep.name).second) {
        return {false, "duplicate symbol across imports: type " + dep.name};
      }
      for (const auto& t : dep.types) {
        if (!seen_type_names.insert(t.name).second) {
          return {false, "duplicate symbol across imports: type " + t.name};
        }
        merged.types.push_back(t);
      }
      for (const auto& e : dep.enums) {
        if (!seen_enum_names.insert(e.name).second) {
          return {false, "duplicate symbol across imports: enum " + e.name};
        }
        merged.enums.push_back(e);
      }
    }
    return {true, ""};
  };

  auto merged_ok = merge_deps(root);
  if (!merged_ok.ok) {
    return merged_ok;
  }

  auto valid = Validate(merged);
  if (!valid.ok) {
    return valid;
  }
  *out = merged;
  return {true, ""};
}

} // namespace kscpp::ir
