#pragma once

#include <map>
#include <string>
#include <vector>

namespace aca::cli {

// Minimal hand-rolled flag parser: no third-party CLI dependency needed.
// Accepts "--key value", "--key=value" and bare "--flag" (value == "true").
class Args {
 public:
  Args(int argc, char** argv, int start);

  bool has(const std::string& key) const;
  std::string str(const std::string& key, const std::string& def = "") const;
  int integer(const std::string& key, int def) const;
  double real(const std::string& key, double def) const;
  bool flag(const std::string& key, bool def) const;

  const std::vector<std::string>& positional() const { return positional_; }

  // Throws if any flag was passed that the command does not understand --
  // catches typos like --frame-size instead of --frame-ms.
  void reject_unknown(const std::vector<std::string>& allowed) const;

 private:
  std::map<std::string, std::string> kv_;
  std::vector<std::string> positional_;
};

// Each command returns a process exit code.
int cmd_encode(const Args& args);
int cmd_decode(const Args& args);
int cmd_aec(const Args& args);
int cmd_ans(const Args& args);
int cmd_agc(const Args& args);
int cmd_pipeline(const Args& args);
int cmd_analyze(const Args& args);
int cmd_live(const Args& args);
int cmd_devices(const Args& args);

}  // namespace aca::cli
