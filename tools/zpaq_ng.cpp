// zpaq_ng.cpp - Command line interface for ZPAQ-NG.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Legacy commands (a/x/l, -m, -method, -all, ...) are byte compatible with the
// original zpaq. Next-generation subcommands (create/benchmark/devices/info/
// profile) run the NG engine. Both share the same binary.

#include "archive/jidac.hpp"
#include "cli/ng_cli.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

using namespace zpaq_ng::archive;

namespace {

// Print help message and exit.
[[noreturn]] void usage() {
  printf(
      "Usage: zpaq_ng command archive[.zpaq] files... -options...\n"
      "Files... may be directory trees. Default is the whole archive.\n"
      "Use * or ??? in archive name for multi-part or \"\" for empty.\n"
      "Commands (legacy, byte compatible with the original zpaq):\n"
      "   a  add         Append files to archive if dates have changed.\n"
      "   x  extract     Extract most recent versions of files.\n"
      "   l  list        List or compare external files to archive by dates.\n"
      "Commands (next generation):\n"
      "   create         NG streaming archive with adaptive compression.\n"
      "   benchmark      Measure ratio and speeds on a corpus or directory.\n"
      "   devices        Report detected CPU/SIMD/RAM and recommended setup.\n"
      "   info           Archive summary (versions, files, fragments, blocks).\n"
      "   profile        Quick multi-level profile on the synthetic corpus.\n"
      "   recover        Extract valid blocks from a corrupted archive.\n"
      "Legacy options:\n"
      "  -all [N]        Extract/list versions in N [4] digit directories.\n"
      "  -f -force       Add: append files if contents have changed.\n"
      "                  Extract: overwrite existing output files.\n"
      "  -mN  -method N  Compress level N (0..5 = faster..better, default 1).\n"
      "  -noattributes   Ignore/don't save file attributes or permissions.\n"
      "  -not files...   Exclude. * and ? match any string or char.\n"
      "  -only files...  Include only matches (default: *).\n"
      "  -sN -summary N  List: show top N sorted by size.\n"
      "  -test           Extract: verify but do not write files.\n"
      "  -to out...      Rename files... to out... or all to out/all.\n"
      "  -until N        Roll back archive to N'th update or -N from end.\n"
      "NG options:\n"
      "  --level ngN     Compression level 0..9 (default ng1).\n"
      "  --threads N     Parallel blocks (default 1; auto = all cores).\n"
      "  --device auto|cpu  Device selection (v1.0 is CPU-only).\n"
      "  --memory 4G     In-memory budget for create.\n"
      "  --dictionary N  Block/dictionary size hint.\n"
      "  --chunk-size N  Content-defined chunk target.\n"
      "  --dedup         Content-defined chunking for dedup-friendly blocks.\n"
      "  --verify        Write SHA-1 checksums (default on).\n"
      "  --json          Machine readable output (benchmark/devices/info).\n"
      "  --verbose       Extra progress and analysis detail.\n"
      "  --deterministic Ordered output (always deterministic).\n");
  exit(1);
}

// Parse the command line into j and execute. Return 1 if error else 0.
int doCommand(Jidac& j, int argc, const char** argv) {
  // Initialize options to default values
  j.command = 0;
  j.force = false;
  j.fragment = 6;
  j.all = 0;
  j.method = "";  // 0..5
  j.noattributes = false;
  j.summary = 0;
  j.dotest = false;  // -test
  j.version = 99999999999999LL;
  j.date = 0;

  printf("zpaq_ng v1.00 journaling archiver (ZPAQ-NG), compiled " __DATE__ "\n");

  // Get date
  std::time_t now = std::time(nullptr);
  std::tm* t = std::gmtime(&now);
  j.date = (t->tm_year + 1900) * 10000000000LL + (t->tm_mon + 1) * 100000000LL +
           t->tm_mday * 1000000 + t->tm_hour * 10000 + t->tm_min * 100 +
           t->tm_sec;

  // Get optional options
  for (int i = 1; i < argc; ++i) {
    const std::string opt = argv[i];
    if ((opt == "add" || opt == "extract" || opt == "list" || opt == "a" ||
         opt == "x" || opt == "l") &&
        i < argc - 1 && argv[i + 1][0] != '-' && j.command == 0) {
      j.command = opt[0];
      if (opt == "extract") j.command = 'x';
      j.archive = argv[++i];  // append ".zpaq" to archive if no extension
      const char* slash = std::strrchr(argv[i], '/');
      const char* dot = std::strrchr(slash ? slash : argv[i], '.');
      if (!dot && j.archive != "") j.archive += ".zpaq";
      while (++i < argc && argv[i][0] != '-')  // read filename args
        j.files.push_back(argv[i]);
      --i;
    } else if (opt.size() < 2 || opt[0] != '-') usage();
    else if (opt == "-all") {
      j.all = 4;
      if (i < argc - 1 && std::isdigit(argv[i + 1][0])) j.all = std::atoi(argv[++i]);
    } else if (opt == "-force" || opt == "-f") j.force = true;
    else if (opt == "-fragment" && i < argc - 1) j.fragment = std::atoi(argv[++i]);
    else if (opt == "-method" && i < argc - 1) j.method = argv[++i];
    else if (opt[1] == 'm') j.method = argv[i] + 2;
    else if (opt == "-noattributes") j.noattributes = true;
    else if (opt == "-not") {  // read notfiles
      while (++i < argc && argv[i][0] != '-') j.notfiles.push_back(argv[i]);
      --i;
    } else if (opt == "-only") {  // read onlyfiles
      while (++i < argc && argv[i][0] != '-') j.onlyfiles.push_back(argv[i]);
      --i;
    } else if (opt == "-summary" && i < argc - 1) j.summary = std::atoi(argv[++i]);
    else if (opt[1] == 's') j.summary = std::atoi(argv[i] + 2);
    else if (opt == "-test") j.dotest = true;
    else if (opt == "-to") {  // read tofiles
      while (++i < argc && argv[i][0] != '-') j.tofiles.push_back(argv[i]);
      if (j.tofiles.size() == 0) j.tofiles.push_back("");
      --i;
    } else if (opt == "-until" && i + 1 < argc) {  // read date
      j.version = 0;
      int digits = 0;
      if (argv[i + 1][0] == '-') {  // negative version
        j.version = std::atol(argv[i + 1]);
        if (j.version > -1) usage();
        ++i;
      } else {  // positive version or date
        while (++i < argc && argv[i][0] != '-') {
          for (int k = 0;; ++k) {
            if (std::isdigit(argv[i][k])) {
              j.version = j.version * 10 + argv[i][k] - '0';
              ++digits;
            } else {
              if (digits == 1) j.version = j.version / 10 * 100 + j.version % 10;
              digits = 0;
              if (argv[i][k] == 0) break;
            }
          }
        }
        --i;
      }
      if (j.version >= 19000000LL && j.version <= 29991231LL)
        j.version = j.version * 100 + 23;
      if (j.version >= 1900000000LL && j.version <= 2999123123LL)
        j.version = j.version * 100 + 59;
      if (j.version >= 190000000000LL && j.version <= 299912312359LL)
        j.version = j.version * 100 + 59;
      if (j.version > 9999999) {
        if (j.version < 19000101000000LL || j.version > 29991231235959LL) {
          fflush(stdout);
          fprintf(stderr,
                  "Version date %1.0f must be 19000101000000 to 29991231235959\n",
                  double(j.version));
          exit(1);
        }
        j.date = j.version;
      }
    } else {
      printf("Unknown option ignored: %s\n", argv[i]);
      usage();
    }
  }

  // Test date
  if (now == -1 || j.date < 19000000000000LL || j.date > 30000000000000LL)
    throw zpaq_ng::invalid_argument_error("date is incorrect, use -until YYYY-MM-DD HH:MM:SS to set");

  // Adjust negative version
  if (j.version < 0) {
    Jidac jidac = j;
    jidac.version = 99999999999999LL;
    jidac.read_archive(j.archive.c_str());
    j.version += jidac.version_count() - 1;
    printf("Version %1.0f\n", j.version + 0.0);
  }

  // Execute command
  if (j.command == 'a' && j.files.size() > 0) return j.add();
  else if (j.command == 'x') return j.extract();
  else if (j.command == 'l') { j.list(); return 0; }
  else usage();
  return 0;
}

}  // namespace

int main(int argc, const char** argv) {
  // In Windows, normalize command line args: convert '\' to '/'.
  std::vector<std::string> args(argc);
  std::vector<const char*> argp(argc);
  for (int i = 0; i < argc; ++i) {
    std::string& s = args[i];
    for (const char* p = argv[i]; *p; ++p) s += (*p == '\\') ? '/' : *p;
    argp[i] = s.c_str();
  }
  int errorcode = 0;
  try {
    if (argc >= 2) {
      const std::string cmd = argp[1];
      if (cmd == "create" || cmd == "benchmark" || cmd == "devices" ||
          cmd == "info" || cmd == "profile" || cmd == "recover") {
        zpaq_ng::cli::NgCliOptions opt;
        std::vector<std::string> positional;
        zpaq_ng::cli::parse_ng_options(argc - 2, argp.data() + 2, opt, positional);
        if (cmd == "create") return zpaq_ng::cli::command_create(opt, positional);
        if (cmd == "benchmark") return zpaq_ng::cli::command_benchmark(opt, positional);
        if (cmd == "devices") return zpaq_ng::cli::command_devices(opt);
        if (cmd == "info") return zpaq_ng::cli::command_info(opt, positional);
        if (cmd == "profile") return zpaq_ng::cli::command_profile(opt);
        if (cmd == "recover") return zpaq_ng::cli::command_recover(opt, positional);
      }
      if (cmd == "test") {
        // Legacy verify: map `test archive [files...]` to `x -test`.
        std::vector<const char*> targs;
        targs.push_back(argp[0]);
        targs.push_back("x");
        targs.push_back("-test");
        for (int i = 2; i < argc; ++i) targs.push_back(argp[i]);
        Jidac jidac;
        errorcode = doCommand(jidac, int(targs.size()), targs.data());
        fflush(stdout);
        return errorcode;
      }
    }
    Jidac jidac;
    errorcode = doCommand(jidac, argc, argp.data());
  } catch (std::exception& e) {
    fflush(stdout);
    fprintf(stderr, "zpaq_ng error: %s\n", e.what());
    errorcode = 2;
  }
  fflush(stdout);
  return errorcode;
}