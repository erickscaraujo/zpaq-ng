// ng_cli.hpp - NG command handlers for the ZPAQ-NG CLI.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Handles the next-generation subcommands (create/benchmark/devices/info)
// and the new option vocabulary. The legacy a/x/l parser in the tool main
// remains untouched; this module only runs when an NG subcommand is named.

#ifndef ZPAQ_NG_CLI_NG_CLI_HPP
#define ZPAQ_NG_CLI_NG_CLI_HPP

#include <string>
#include <vector>

namespace zpaq_ng::cli {

// Options shared by the NG subcommands.
struct NgCliOptions {
  int level = 1;              // ng level 0..9 (create/benchmark)
  unsigned threads = 1;       // 0 = auto
  std::string device = "auto";
  std::size_t memory = 0;     // 0 = auto
  std::size_t dictionary = 0; // 0 = auto
  std::size_t chunk_size = 0; // 0 = auto
  bool dedup = false;
  bool verify = true;
  bool progress = false;
  bool json = false;
  bool verbose = false;
  bool deterministic = false;
  bool compare = false;       // benchmark --compare
};

// Parse NG options (--level, --threads, --device, --memory, --dictionary,
// --chunk-size, --dedup, --verify, --progress, --json, --verbose,
// --deterministic, --compare). Throws zpaq_ng::invalid_argument_error.
// Positional arguments are returned in positional.
void parse_ng_options(int argc, const char** argv, NgCliOptions& opt,
                      std::vector<std::string>& positional);

// `create <archive> <files...>`: NG streaming create. Returns process exit
// code (0 ok, 1 error). Archives written here are readable by the original.
int command_create(const NgCliOptions& opt, const std::vector<std::string>& args);

// `benchmark [dir]`: run the benchmark engine. Returns 0.
int command_benchmark(const NgCliOptions& opt, const std::vector<std::string>& args);

// `devices`: hardware report. Returns 0.
int command_devices(const NgCliOptions& opt);

// `info <archive>`: archive summary. Returns 0.
int command_info(const NgCliOptions& opt, const std::vector<std::string>& args);

// `profile`: quick self-profile across levels. Returns 0.
int command_profile(const NgCliOptions& opt);

// `recover <archive> [out]`: extract valid blocks from a corrupted streaming
// archive; report what was recovered. Returns 0.
int command_recover(const NgCliOptions& opt, const std::vector<std::string>& args);

} // namespace zpaq_ng::cli

#endif // ZPAQ_NG_CLI_NG_CLI_HPP