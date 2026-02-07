#include "codegen.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace kscpp::codegen {
namespace {

Result ValidateSupportedSubset(const ir::Spec& spec) {
  if (!spec.types.empty()) {
    return {false, "not yet supported: type definitions in IR (types section)"};
  }
  if (!spec.instances.empty()) {
    return {false, "not yet supported: instances in IR"};
  }
  if (!spec.validations.empty()) {
    return {false, "not yet supported: validations in IR"};
  }

  for (const auto& attr : spec.attrs) {
    if (attr.endian_override.has_value()) {
      return {false, "not yet supported: attr.endian_override"};
    }
    if (attr.size_expr.has_value()) {
      return {false, "not yet supported: sized attrs via attr.size_expr"};
    }
    if (attr.type.kind != ir::TypeRef::Kind::kPrimitive) {
      return {false, "not yet supported: user-defined attr types"};
    }
    switch (attr.type.primitive) {
    case ir::PrimitiveType::kU1:
    case ir::PrimitiveType::kU2:
    case ir::PrimitiveType::kU4:
    case ir::PrimitiveType::kU8:
    case ir::PrimitiveType::kS1:
    case ir::PrimitiveType::kS2:
    case ir::PrimitiveType::kS4:
    case ir::PrimitiveType::kS8:
      break;
    default:
      return {false, "not yet supported: primitive attr type in this migration slice"};
    }
  }

  return {true, ""};
}

std::string CppFieldType(ir::PrimitiveType primitive) {
  switch (primitive) {
  case ir::PrimitiveType::kU1:
    return "uint8_t";
  case ir::PrimitiveType::kU2:
    return "uint16_t";
  case ir::PrimitiveType::kU4:
    return "uint32_t";
  case ir::PrimitiveType::kU8:
    return "uint64_t";
  case ir::PrimitiveType::kS1:
    return "int8_t";
  case ir::PrimitiveType::kS2:
    return "int16_t";
  case ir::PrimitiveType::kS4:
    return "int32_t";
  case ir::PrimitiveType::kS8:
    return "int64_t";
  default:
    return "uint8_t";
  }
}

std::string ReadMethod(ir::PrimitiveType primitive, ir::Endian endian) {
  const bool be = endian == ir::Endian::kBe;
  switch (primitive) {
  case ir::PrimitiveType::kU1:
    return "read_u1";
  case ir::PrimitiveType::kU2:
    return be ? "read_u2be" : "read_u2le";
  case ir::PrimitiveType::kU4:
    return be ? "read_u4be" : "read_u4le";
  case ir::PrimitiveType::kU8:
    return be ? "read_u8be" : "read_u8le";
  case ir::PrimitiveType::kS1:
    return "read_s1";
  case ir::PrimitiveType::kS2:
    return be ? "read_s2be" : "read_s2le";
  case ir::PrimitiveType::kS4:
    return be ? "read_s4be" : "read_s4le";
  case ir::PrimitiveType::kS8:
    return be ? "read_s8be" : "read_s8le";
  default:
    return "read_u1";
  }
}

std::string RenderHeader(const ir::Spec& spec) {
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
  out << "    " << spec.name << "_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent = nullptr, "
      << spec.name << "_t* p__root = nullptr);\n\n";
  out << "private:\n";
  out << "    void _read();\n";
  out << "    void _clean_up();\n\n";
  out << "public:\n";
  out << "    ~" << spec.name << "_t();\n";
  for (const auto& attr : spec.attrs) {
    out << "    " << CppFieldType(attr.type.primitive) << " " << attr.id << "() const { return m_"
        << attr.id << "; }\n";
  }
  out << "    " << spec.name << "_t* _root() const { return m__root; }\n";
  out << "    kaitai::kstruct* _parent() const { return m__parent; }\n\n";
  out << "private:\n";
  for (const auto& attr : spec.attrs) {
    out << "    " << CppFieldType(attr.type.primitive) << " m_" << attr.id << ";\n";
  }
  out << "    " << spec.name << "_t* m__root;\n";
  out << "    kaitai::kstruct* m__parent;\n";
  out << "};\n";
  return out.str();
}

std::string RenderSource(const ir::Spec& spec) {
  std::ostringstream out;
  out << "// This is a generated file! Please edit source .ksy file and use kaitai-struct-compiler to rebuild\n\n";
  out << "#include \"" << spec.name << ".h\"\n\n";
  out << spec.name << "_t::" << spec.name
      << "_t(kaitai::kstream* p__io, kaitai::kstruct* p__parent, " << spec.name
      << "_t* p__root) : kaitai::kstruct(p__io) {\n";
  out << "    m__parent = p__parent;\n";
  out << "    m__root = p__root ? p__root : this;\n";
  out << "    _read();\n";
  out << "}\n\n";

  out << "void " << spec.name << "_t::_read() {\n";
  for (const auto& attr : spec.attrs) {
    out << "    m_" << attr.id << " = m__io->"
        << ReadMethod(attr.type.primitive, spec.default_endian) << "();\n";
  }
  out << "}\n\n";

  out << spec.name << "_t::~" << spec.name << "_t() {\n";
  out << "    _clean_up();\n";
  out << "}\n\n";

  out << "void " << spec.name << "_t::_clean_up() {\n";
  out << "}\n";
  return out.str();
}

} // namespace

Result EmitCppStl17FromIr(const ir::Spec& spec, const CliOptions& options) {
  const auto validate_subset = ValidateSupportedSubset(spec);
  if (!validate_subset.ok) {
    return validate_subset;
  }

  const std::filesystem::path out_dir(options.out_dir);
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);
  if (ec) {
    return {false, "failed to create output directory: " + ec.message()};
  }

  const std::filesystem::path header_path = out_dir / (spec.name + ".h");
  const std::filesystem::path source_path = out_dir / (spec.name + ".cpp");

  std::ofstream header(header_path);
  if (!header) {
    return {false, "failed to open output file: " + header_path.string()};
  }
  std::ofstream source(source_path);
  if (!source) {
    return {false, "failed to open output file: " + source_path.string()};
  }

  header << RenderHeader(spec);
  source << RenderSource(spec);
  return {true, ""};
}

} // namespace kscpp::codegen
