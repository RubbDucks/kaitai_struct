#include "frontend.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_set>

namespace kscpp::frontend {
namespace {

Result Err(std::string message) { return {false, std::move(message)}; }
Result Ok() { return {true, ""}; }

struct KsyDoc {
  std::string id;
  std::vector<std::string> imports;
  struct SeqEntry {
    std::string id;
    std::string type;
    std::string if_expr;
  };
  std::vector<SeqEntry> seq;
};

std::string Trim(const std::string& s) {
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
  size_t b = s.size();
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
  return s.substr(a, b - a);
}

int IndentOf(const std::string& s) {
  int n = 0;
  while (n < static_cast<int>(s.size()) && s[n] == ' ') n++;
  return n;
}

bool StartsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

Result ReadFile(const std::filesystem::path& path, std::string* out) {
  std::ifstream in(path);
  if (!in) return Err("unable to open file: " + path.string());
  std::ostringstream buffer;
  buffer << in.rdbuf();
  *out = buffer.str();
  return Ok();
}

std::optional<ir::PrimitiveType> ParsePrimitive(const std::string& t) {
  if (t == "u1") return ir::PrimitiveType::kU1;
  if (t == "u2") return ir::PrimitiveType::kU2;
  if (t == "u4") return ir::PrimitiveType::kU4;
  if (t == "u8") return ir::PrimitiveType::kU8;
  if (t == "s1") return ir::PrimitiveType::kS1;
  if (t == "s2") return ir::PrimitiveType::kS2;
  if (t == "s4") return ir::PrimitiveType::kS4;
  if (t == "s8") return ir::PrimitiveType::kS8;
  if (t == "f4") return ir::PrimitiveType::kF4;
  if (t == "f8") return ir::PrimitiveType::kF8;
  if (t == "str") return ir::PrimitiveType::kStr;
  if (t == "bytes") return ir::PrimitiveType::kBytes;
  return std::nullopt;
}

bool LooksMalformedExpr(const std::string& expr) {
  if (expr.empty()) return true;
  int depth = 0;
  for (char c : expr) {
    if (c == '(') depth++;
    if (c == ')') depth--;
    if (depth < 0) return true;
  }
  if (depth != 0) return true;
  return false;
}

std::string DefaultModuleName(const std::filesystem::path& p) {
  return p.stem().string();
}

Result ParseKsy(const std::filesystem::path& path, KsyDoc* out) {
  std::string raw;
  auto r = ReadFile(path, &raw);
  if (!r.ok) return r;

  std::vector<std::string> lines;
  {
    std::istringstream in(raw);
    std::string line;
    while (std::getline(in, line)) {
      const auto hash = line.find('#');
      if (hash != std::string::npos) line = line.substr(0, hash);
      lines.push_back(line);
    }
  }

  out->id.clear();
  out->imports.clear();
  out->seq.clear();

  enum class Section { kNone, kMeta, kImports, kSeq };
  Section section = Section::kNone;
  KsyDoc::SeqEntry* current = nullptr;

  for (size_t i = 0; i < lines.size(); i++) {
    const std::string trimmed = Trim(lines[i]);
    if (trimmed.empty()) continue;

    if (trimmed == "meta:") {
      section = Section::kMeta;
      current = nullptr;
      continue;
    }
    if (trimmed == "imports:") {
      section = Section::kImports;
      current = nullptr;
      continue;
    }
    if (trimmed == "seq:") {
      section = Section::kSeq;
      current = nullptr;
      continue;
    }

    if (section == Section::kMeta && StartsWith(trimmed, "id:")) {
      out->id = Trim(trimmed.substr(3));
      continue;
    }

    if (section == Section::kImports && StartsWith(trimmed, "- ")) {
      out->imports.push_back(Trim(trimmed.substr(2)));
      continue;
    }

    if (section == Section::kSeq) {
      const int indent = IndentOf(lines[i]);
      if (StartsWith(trimmed, "- ")) {
        out->seq.push_back({});
        current = &out->seq.back();
        const std::string rest = Trim(trimmed.substr(2));
        if (StartsWith(rest, "id:")) {
          current->id = Trim(rest.substr(3));
        }
        continue;
      }
      if (indent >= 2 && current != nullptr) {
        if (StartsWith(trimmed, "id:")) current->id = Trim(trimmed.substr(3));
        if (StartsWith(trimmed, "type:")) current->type = Trim(trimmed.substr(5));
        if (StartsWith(trimmed, "if:")) current->if_expr = Trim(trimmed.substr(3));
      }
    }
  }

  if (out->id.empty()) out->id = DefaultModuleName(path);
  return Ok();
}

Result BuildSpec(const std::filesystem::path& source_path, const KsyDoc& doc, ir::Spec* spec) {
  spec->name = doc.id;
  spec->default_endian = ir::Endian::kLe;
  spec->imports = doc.imports;

  for (const auto& entry : doc.seq) {
    if (entry.id.empty()) {
      return Err("ParseError: seq item missing id in " + source_path.string());
    }
    if (entry.type.empty()) {
      return Err("ParseError: seq item missing type in " + source_path.string());
    }

    ir::Attr a;
    a.id = entry.id;
    if (const auto prim = ParsePrimitive(entry.type); prim.has_value()) {
      a.type.kind = ir::TypeRef::Kind::kPrimitive;
      a.type.primitive = *prim;
    } else {
      a.type.kind = ir::TypeRef::Kind::kUser;
      a.type.user_type = entry.type;
    }

    if (!entry.if_expr.empty()) {
      if (LooksMalformedExpr(entry.if_expr)) {
        return Err("ExpressionError: malformed expression in if: " + entry.if_expr);
      }
      a.if_expr = ir::Expr::Name(entry.if_expr);
    }

    spec->attrs.push_back(std::move(a));
  }

  return Ok();
}

bool EndsWith(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

Result ResolveOneImport(const std::filesystem::path& from,
                        const std::string& import_name,
                        const std::vector<std::string>& import_paths,
                        std::filesystem::path* out) {
  std::string n = import_name;
  if (!EndsWith(n, ".ksy")) n += ".ksy";

  std::vector<std::filesystem::path> candidates;
  candidates.push_back(from.parent_path() / n);
  for (const auto& p : import_paths) candidates.push_back(std::filesystem::path(p) / n);

  for (const auto& c : candidates) {
    std::error_code ec;
    auto canon = std::filesystem::weakly_canonical(c, ec);
    if (!ec && std::filesystem::exists(canon)) {
      *out = canon;
      return Ok();
    }
  }
  return Err("ImportError: unable to resolve import '" + import_name + "' from " + from.string());
}

} // namespace

Result ParseKsyInputs(const CliOptions& options, ParsedInputs* out) {
  out->files.clear();
  out->lowered_by_path.clear();
  if (options.src_files.empty()) return Err("no source .ksy files provided");

  std::unordered_set<std::string> seen;
  for (const auto& src : options.src_files) {
    std::filesystem::path path(src);
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec || !std::filesystem::exists(canonical)) return Err("source file not found: " + src);
    if (canonical.extension() != ".ksy") return Err("source file must have .ksy extension: " + canonical.string());
    const std::string key = canonical.string();
    if (!seen.insert(key).second) continue;

    KsyDoc doc;
    auto pr = ParseKsy(canonical, &doc);
    if (!pr.ok) return pr;

    ParsedInput input;
    input.source_path = canonical;
    input.module_name = doc.id;
    input.imports = doc.imports;
    out->files.push_back(std::move(input));

    ir::Spec spec;
    auto br = BuildSpec(canonical, doc, &spec);
    if (!br.ok) return br;
    out->lowered_by_path[key] = std::move(spec);
  }

  if (out->files.empty()) return Err("no source .ksy files provided");
  return Ok();
}

Result ResolveImports(const CliOptions& options, ParsedInputs* parsed) {
  std::unordered_map<std::string, std::filesystem::path> module_to_path;
  for (const auto& in : parsed->files) {
    auto [it, inserted] = module_to_path.emplace(in.module_name, in.source_path);
    if (!inserted && it->second != in.source_path) {
      return Err("ImportCollisionError: module id collision '" + in.module_name + "' between " +
                 it->second.string() + " and " + in.source_path.string());
    }
  }

  std::set<std::string> visited;
  std::set<std::string> active;
  std::function<Result(const std::filesystem::path&)> dfs = [&](const std::filesystem::path& p) -> Result {
    const std::string key = p.string();
    if (active.count(key)) return Err("ImportCycleError: import cycle detected at " + key);
    if (visited.count(key)) return Ok();

    active.insert(key);
    visited.insert(key);

    auto found = std::find_if(parsed->files.begin(), parsed->files.end(), [&](const ParsedInput& x) {
      return x.source_path == p;
    });
    if (found == parsed->files.end()) {
      KsyDoc doc;
      auto pr = ParseKsy(p, &doc);
      if (!pr.ok) return pr;
      ParsedInput next;
      next.source_path = p;
      next.module_name = doc.id;
      next.imports = doc.imports;
      parsed->files.push_back(next);

      ir::Spec spec;
      auto br = BuildSpec(p, doc, &spec);
      if (!br.ok) return br;
      parsed->lowered_by_path[p.string()] = std::move(spec);
      found = std::prev(parsed->files.end());
    }

    for (const auto& imp : found->imports) {
      std::filesystem::path resolved;
      auto rr = ResolveOneImport(found->source_path, imp, options.import_paths, &resolved);
      if (!rr.ok) return rr;
      auto dr = dfs(resolved);
      if (!dr.ok) return dr;
    }

    active.erase(key);
    return Ok();
  };

  const size_t roots = parsed->files.size();
  for (size_t i = 0; i < roots; i++) {
    auto r = dfs(parsed->files[i].source_path);
    if (!r.ok) return r;
  }

  return Ok();
}

Result LowerToIr(const CliOptions&,
                 const ParsedInputs& parsed,
                 std::vector<ir::Spec>* out_specs) {
  out_specs->clear();
  for (const auto& input : parsed.files) {
    auto it = parsed.lowered_by_path.find(input.source_path.string());
    if (it == parsed.lowered_by_path.end()) {
      return Err("LoweringError: missing parsed module for " + input.source_path.string());
    }
    out_specs->push_back(it->second);
  }
  return Ok();
}

Result ValidateSemanticsAndTypes(const std::vector<ir::Spec>& specs) {
  for (const auto& spec : specs) {
    const auto result = ir::Validate(spec);
    if (!result.ok) {
      return Err("semantic/type validation failed for " + spec.name + ": " + result.error);
    }

    std::unordered_set<std::string> local_types;
    for (const auto& td : spec.types) local_types.insert(td.name);
    for (const auto& attr : spec.attrs) {
      if (attr.type.kind == ir::TypeRef::Kind::kUser && !local_types.count(attr.type.user_type)) {
        return Err("TypeError: unknown type: " + attr.type.user_type + " in spec " + spec.name);
      }
    }
  }
  return Ok();
}

} // namespace kscpp::frontend
