#include <filesystem>
#include <fstream>
#include <iostream>

#include "cli_options.h"
#include "frontend.h"

namespace {

bool Check(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << std::endl;
    return false;
  }
  return true;
}

void Write(const std::filesystem::path& p, const std::string& txt) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream o(p);
  o << txt;
}

} // namespace

int main() {
  bool ok = true;
  const auto root = std::filesystem::temp_directory_path() / "kscpp_frontend_tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  {
    const auto p = root / "parse_ok.ksy";
    Write(p, "meta:\n  id: parse_ok\nseq:\n  - id: x\n    type: u1\n");

    kscpp::CliOptions opt;
    opt.src_files = {p.string()};
    kscpp::frontend::ParsedInputs parsed;
    auto r = kscpp::frontend::ParseKsyInputs(opt, &parsed);
    ok &= Check(r.ok, "parse minimal .ksy succeeds");

    std::vector<kscpp::ir::Spec> specs;
    auto l = kscpp::frontend::LowerToIr(opt, parsed, &specs);
    ok &= Check(l.ok && specs.size() == 1 && specs[0].name == "parse_ok", "lowering creates spec");
  }

  {
    const auto p = root / "bad_expr.ksy";
    Write(p, "meta:\n  id: bad_expr\nseq:\n  - id: x\n    type: u1\n    if: (foo\n");

    kscpp::CliOptions opt;
    opt.src_files = {p.string()};
    kscpp::frontend::ParsedInputs parsed;
    auto r = kscpp::frontend::ParseKsyInputs(opt, &parsed);
    ok &= Check(!r.ok && r.error.find("ExpressionError") != std::string::npos,
                "malformed expression rejected");
  }

  {
    const auto p = root / "unknown_type.ksy";
    Write(p, "meta:\n  id: unknown_type\nseq:\n  - id: x\n    type: not_declared\n");

    kscpp::CliOptions opt;
    opt.src_files = {p.string()};
    kscpp::frontend::ParsedInputs parsed;
    auto r = kscpp::frontend::ParseKsyInputs(opt, &parsed);
    ok &= Check(r.ok, "parse unknown type fixture succeeds");

    std::vector<kscpp::ir::Spec> specs;
    auto l = kscpp::frontend::LowerToIr(opt, parsed, &specs);
    ok &= Check(l.ok, "lower unknown type fixture succeeds");
    auto v = kscpp::frontend::ValidateSemanticsAndTypes(specs);
    ok &= Check(!v.ok, "unknown type failure is surfaced");
  }

  {
    const auto a = root / "cycle_a.ksy";
    const auto b = root / "cycle_b.ksy";
    Write(a, "meta:\n  id: cycle_a\nimports:\n  - cycle_b\nseq:\n  - id: x\n    type: u1\n");
    Write(b, "meta:\n  id: cycle_b\nimports:\n  - cycle_a\nseq:\n  - id: y\n    type: u1\n");

    kscpp::CliOptions opt;
    opt.src_files = {a.string()};
    kscpp::frontend::ParsedInputs parsed;
    auto r = kscpp::frontend::ParseKsyInputs(opt, &parsed);
    ok &= Check(r.ok, "parse cycle fixture succeeds");
    auto imp = kscpp::frontend::ResolveImports(opt, &parsed);
    ok &= Check(!imp.ok && imp.error.find("ImportCycleError") != std::string::npos,
                "import cycle detected");
  }

  {
    const auto a = root / "collide_a.ksy";
    const auto b = root / "collide_b.ksy";
    Write(a, "meta:\n  id: same_name\nseq:\n  - id: x\n    type: u1\n");
    Write(b, "meta:\n  id: same_name\nseq:\n  - id: y\n    type: u1\n");

    kscpp::CliOptions opt;
    opt.src_files = {a.string(), b.string()};
    kscpp::frontend::ParsedInputs parsed;
    auto r = kscpp::frontend::ParseKsyInputs(opt, &parsed);
    ok &= Check(r.ok, "parse collision fixtures succeeds");
    auto imp = kscpp::frontend::ResolveImports(opt, &parsed);
    ok &= Check(!imp.ok && imp.error.find("ImportCollisionError") != std::string::npos,
                "import collision detected");
  }

  return ok ? 0 : 1;
}
