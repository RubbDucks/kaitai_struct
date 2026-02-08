#ifndef KAITAI_STRUCT_COMPILER_CPP_IR_H_
#define KAITAI_STRUCT_COMPILER_CPP_IR_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kscpp::ir {

enum class Endian {
  kLe,
  kBe,
};

enum class PrimitiveType {
  kU1,
  kU2,
  kU4,
  kU8,
  kS1,
  kS2,
  kS4,
  kS8,
  kF4,
  kF8,
  kStr,
  kBytes,
};

struct Expr {
  enum class Kind {
    kInt,
    kBool,
    kName,
    kUnary,
    kBinary,
  };

  Kind kind = Kind::kInt;
  long long int_value = 0;
  bool bool_value = false;
  std::string text;
  std::shared_ptr<Expr> lhs;
  std::shared_ptr<Expr> rhs;

  static Expr Int(long long value);
  static Expr Bool(bool value);
  static Expr Name(std::string value);
  static Expr Unary(std::string op, Expr value);
  static Expr Binary(std::string op, Expr left, Expr right);
};

struct TypeRef {
  enum class Kind {
    kPrimitive,
    kUser,
  };

  Kind kind = Kind::kPrimitive;
  PrimitiveType primitive = PrimitiveType::kU1;
  std::string user_type;
};

struct TypeDef {
  std::string name;
  TypeRef type;
};

struct Attr {
  enum class RepeatKind {
    kNone,
    kEos,
    kExpr,
    kUntil,
  };

  struct SwitchCase {
    std::optional<Expr> match_expr;
    TypeRef type;
  };

  std::string id;
  TypeRef type;
  std::optional<Endian> endian_override;
  std::optional<Expr> size_expr;
  std::optional<std::string> enum_name;
  std::optional<std::string> encoding;
  std::optional<Expr> if_expr;
  RepeatKind repeat = RepeatKind::kNone;
  std::optional<Expr> repeat_expr;
  std::optional<Expr> switch_on;
  std::vector<SwitchCase> switch_cases;
};

struct EnumValue {
  long long value = 0;
  std::string name;
};

struct EnumDef {
  std::string name;
  std::vector<EnumValue> values;
};

struct Instance {
  std::string id;
  Expr value_expr;
};

struct Validation {
  std::string target;
  Expr condition_expr;
  std::string message;
};

struct Spec {
  std::string name;
  Endian default_endian = Endian::kLe;
  std::vector<std::string> imports;
  std::vector<TypeDef> types;
  std::vector<Attr> attrs;
  std::vector<EnumDef> enums;
  std::vector<Instance> instances;
  std::vector<Validation> validations;
};

struct ValidationResult {
  bool ok = false;
  std::string error;
};

ValidationResult Validate(const Spec& spec);
std::string Serialize(const Spec& spec);
ValidationResult Deserialize(const std::string& encoded, Spec* out, bool validate = true);
ValidationResult LoadFromFile(const std::string& path, Spec* out);
ValidationResult LoadFromFileWithImports(const std::string& path,
                                         const std::vector<std::string>& import_paths,
                                         Spec* out);

} // namespace kscpp::ir

#endif
