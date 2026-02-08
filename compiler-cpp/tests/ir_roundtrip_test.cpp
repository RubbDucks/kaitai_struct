#include <iostream>
#include <string>

#include "ir.h"

namespace {

bool Check(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    return false;
  }
  return true;
}

} // namespace

int main() {
  bool ok = true;

  {
    kscpp::ir::Spec spec;
    spec.name = "packet_header";
    spec.default_endian = kscpp::ir::Endian::kBe;

    kscpp::ir::TypeDef typ;
    typ.name = "counter_t";
    typ.type.kind = kscpp::ir::TypeRef::Kind::kPrimitive;
    typ.type.primitive = kscpp::ir::PrimitiveType::kU4;
    spec.types.push_back(typ);

    kscpp::ir::Attr attr;
    attr.id = "payload";
    attr.type.kind = kscpp::ir::TypeRef::Kind::kPrimitive;
    attr.type.primitive = kscpp::ir::PrimitiveType::kBytes;
    attr.size_expr =
        kscpp::ir::Expr::Binary("+", kscpp::ir::Expr::Name("len"), kscpp::ir::Expr::Int(4));
    spec.attrs.push_back(attr);

    kscpp::ir::Attr title;
    title.id = "title";
    title.type.kind = kscpp::ir::TypeRef::Kind::kPrimitive;
    title.type.primitive = kscpp::ir::PrimitiveType::kStr;
    title.size_expr = kscpp::ir::Expr::Int(8);
    title.encoding = "UTF-8";
    spec.attrs.push_back(title);

    kscpp::ir::EnumDef enum_def;
    enum_def.name = "packet_kind";
    enum_def.values.push_back({1, "request"});
    enum_def.values.push_back({2, "response"});
    spec.enums.push_back(enum_def);

    kscpp::ir::Instance inst;
    inst.id = "checksum_ok";
    inst.value_expr = kscpp::ir::Expr::Unary("!", kscpp::ir::Expr::Name("bad_checksum"));
    spec.instances.push_back(inst);

    kscpp::ir::Validation validation;
    validation.target = "len";
    validation.condition_expr =
        kscpp::ir::Expr::Binary(">=", kscpp::ir::Expr::Name("len"), kscpp::ir::Expr::Int(0));
    validation.message = "len must be non-negative";
    spec.validations.push_back(validation);

    auto validate = kscpp::ir::Validate(spec);
    ok &= Check(validate.ok, "valid IR validates");

    std::string encoded = kscpp::ir::Serialize(spec);
    kscpp::ir::Spec decoded;
    auto parse = kscpp::ir::Deserialize(encoded, &decoded);
    ok &= Check(parse.ok, "serialized IR can be parsed");

    ok &= Check(decoded.enums.size() == 1, "enum definitions survive round-trip");
    ok &= Check(decoded.attrs.size() == 2, "attrs survive round-trip");
    ok &= Check(decoded.attrs[1].encoding.has_value() && *decoded.attrs[1].encoding == "UTF-8",
                "string encoding survives round-trip");

    std::string reencoded = kscpp::ir::Serialize(decoded);
    ok &= Check(encoded == reencoded, "IR round-trip is stable");
  }

  {
    kscpp::ir::Spec invalid;
    invalid.default_endian = kscpp::ir::Endian::kLe;
    auto r = kscpp::ir::Validate(invalid);
    ok &= Check(!r.ok, "missing spec name rejected");
  }

  {
    kscpp::ir::Spec invalid;
    invalid.name = "x";
    invalid.default_endian = kscpp::ir::Endian::kLe;
    kscpp::ir::TypeDef alias;
    alias.name = "len_t";
    alias.type.kind = kscpp::ir::TypeRef::Kind::kUser;
    alias.type.user_type = "missing_t";
    invalid.types.push_back(alias);
    auto r = kscpp::ir::Validate(invalid);
    ok &= Check(!r.ok, "unknown type reference rejected");
    ok &= Check(r.error.find("unknown user type") != std::string::npos,
                "unknown type diagnostic is clear");
  }

  {
    kscpp::ir::Spec invalid;
    invalid.name = "x";
    invalid.default_endian = kscpp::ir::Endian::kLe;

    kscpp::ir::TypeDef a;
    a.name = "a_t";
    a.type.kind = kscpp::ir::TypeRef::Kind::kUser;
    a.type.user_type = "b_t";
    invalid.types.push_back(a);

    kscpp::ir::TypeDef b;
    b.name = "b_t";
    b.type.kind = kscpp::ir::TypeRef::Kind::kUser;
    b.type.user_type = "a_t";
    invalid.types.push_back(b);

    auto r = kscpp::ir::Validate(invalid);
    ok &= Check(!r.ok, "type alias cycle rejected");
    ok &= Check(r.error.find("cycle") != std::string::npos, "cycle diagnostic is clear");
  }

  {
    kscpp::ir::Spec imported;
    auto r = kscpp::ir::LoadFromFileWithImports("../tests/data/imports_nested_root.ksir", {"../tests/data"}, &imported);
    ok &= Check(r.ok, "nested imports load and merge");
    ok &= Check(imported.types.size() == 5, "merged imported types are available");
  }

  {
    kscpp::ir::Spec imported;
    auto r = kscpp::ir::LoadFromFileWithImports("../tests/data/imports/cycle/a.ksir", {"../tests/data"}, &imported);
    ok &= Check(!r.ok, "import cycle rejected");
    ok &= Check(r.error.find("import cycle detected") != std::string::npos,
                "import cycle diagnostic is clear");
  }

  {
    kscpp::ir::Spec imported;
    auto r = kscpp::ir::LoadFromFileWithImports("../tests/data/imports/collision/root.ksir", {"../tests/data"}, &imported);
    ok &= Check(!r.ok, "duplicate imported symbol rejected");
    ok &= Check(r.error.find("duplicate symbol across imports") != std::string::npos,
                "duplicate symbol diagnostic is clear");
  }

  return ok ? 0 : 1;
}
