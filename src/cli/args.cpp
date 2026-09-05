#include <stdexcept>
#include <string>
#include <vector>

#include "aca/cli_args.h"

namespace aca::cli {
namespace {

// True if `tok` should be consumed as the value of the preceding flag.
// Anything not starting with '-' is a value; a token starting with '-' is only
// a value when it is a negative number (e.g. "-25", "-3.5"), so that
// "--suppress-db -25" works while "--dereverb --frame-ms 10" still does not
// swallow the next option.
bool is_value_token(const char* tok) {
  if (tok == nullptr || tok[0] == '\0') return false;
  if (tok[0] != '-') return true;
  // "-" alone, or "-x" where x is not a digit/dot, is an option.
  const char* p = tok + 1;
  if (*p == '\0') return false;
  if (*p == '.') ++p;
  return *p >= '0' && *p <= '9';
}

}  // namespace

Args::Args(int argc, char** argv, int start) {
  for (int i = start; i < argc; ++i) {
    std::string tok = argv[i];

    if (tok.rfind("--", 0) == 0) {
      std::string key = tok.substr(2);
      const auto eq = key.find('=');
      if (eq != std::string::npos) {
        kv_[key.substr(0, eq)] = key.substr(eq + 1);
        continue;
      }
      // A following token is this flag's value unless it is another option.
      // A leading '-' is NOT sufficient to rule it out: negative numbers like
      // "--suppress-db -25" and "--target-dbfs -3.5" are legitimate values.
      if (i + 1 < argc && is_value_token(argv[i + 1])) {
        kv_[key] = argv[++i];
      } else {
        kv_[key] = "true";
      }
      continue;
    }

    // -o is the only short option, and it always takes a value.
    if (tok == "-o") {
      if (i + 1 >= argc) throw std::runtime_error("-o requires a path");
      kv_["out"] = argv[++i];
      continue;
    }

    positional_.push_back(std::move(tok));
  }
}

bool Args::has(const std::string& key) const { return kv_.count(key) > 0; }

std::string Args::str(const std::string& key, const std::string& def) const {
  const auto it = kv_.find(key);
  return it == kv_.end() ? def : it->second;
}

int Args::integer(const std::string& key, int def) const {
  const auto it = kv_.find(key);
  if (it == kv_.end()) return def;
  try {
    return std::stoi(it->second);
  } catch (...) {
    throw std::runtime_error("--" + key + " expects an integer, got '" +
                             it->second + "'");
  }
}

double Args::real(const std::string& key, double def) const {
  const auto it = kv_.find(key);
  if (it == kv_.end()) return def;
  try {
    return std::stod(it->second);
  } catch (...) {
    throw std::runtime_error("--" + key + " expects a number, got '" +
                             it->second + "'");
  }
}

bool Args::flag(const std::string& key, bool def) const {
  const auto it = kv_.find(key);
  if (it == kv_.end()) return def;
  const std::string& v = it->second;
  if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
  if (v == "false" || v == "0" || v == "no" || v == "off") return false;
  throw std::runtime_error("--" + key + " expects true/false, got '" + v + "'");
}

void Args::reject_unknown(const std::vector<std::string>& allowed) const {
  for (const auto& [k, v] : kv_) {
    bool ok = false;
    for (const auto& a : allowed) {
      if (k == a) {
        ok = true;
        break;
      }
    }
    if (!ok) throw std::runtime_error("unknown option '--" + k + "'");
  }
}

}  // namespace aca::cli
