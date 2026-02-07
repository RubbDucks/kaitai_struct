#include <iostream>
#include <string_view>

namespace {
constexpr std::string_view kVersion = "0.0.0";

void print_version() {
  std::cout << "kscpp " << kVersion << " experimental" << std::endl;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--version") {
    print_version();
    return 0;
  }

  print_version();
  return 0;
}
