#include "searcher.h"
#include "result.h"

#include <iostream>
#include <string>
#include <chrono>
#include <cstring>
#include <stdexcept>

// ANSI color codes - not doing anything fancy, just the basics
// these wont work on windows cmd without enabling VT mode but whatever
#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_RED     "\033[31m"
#define CLR_GREEN   "\033[32m"
#define CLR_YELLOW  "\033[33m"
#define CLR_CYAN    "\033[36m"
#define CLR_MAGENTA "\033[35m"

static bool colorEnabled = true;

// wraps text in color only if colors are on
static std::string colored(const std::string& text, const char* code) {
    if (!colorEnabled) return text;
    return std::string(code) + text + CLR_RESET;
}

static void printUsage(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [options] <pattern> <path>\n\n";
    std::cout << "Options:\n";
    std::cout << "  -i           case insensitive search\n";
    std::cout << "  -E           treat pattern as extended regex\n";
    std::cout << "  -c           count matches only (dont print lines)\n";
    std::cout << "  -n           hide line numbers\n";
    std::cout << "  --no-color   disable colored output\n";
    std::cout << "  --ext=EXT    only search files with this extension (repeatable)\n";
    std::cout << "               e.g. --ext=.cpp --ext=.h\n";
    std::cout << "  --depth=N    max directory depth (-1 = unlimited, default)\n";
    std::cout << "\nExample:\n";
    std::cout << "  " << argv0 << " -i -E \"TODO|FIXME\" ./src --ext=.cpp\n";
}

// print one file's results to stdout
static void printFileResult(const FileResult& fr, const SearchConfig& cfg) {
    std::string fname = colored(fr.filepath, CLR_MAGENTA);

    if (cfg.countOnly) {
        std::cout << fname << ": " << colored(std::to_string(fr.matches.size()), CLR_YELLOW) << "\n";
        return;
    }

    // print file header
    std::cout << "\n" << colored("==> ", CLR_CYAN) << fname << colored(" <==", CLR_CYAN) << "\n";

    // deduplicate lines - if the same line number appears multiple times
    // (multiple matches on same line) we only print the line once but highlight all matches
    int lastPrinted = -1;
    for (size_t i = 0; i < fr.matches.size(); i++) {
        const Match& m = fr.matches[i];

        if (m.lineNum == lastPrinted) continue; // already printed this line
        lastPrinted = m.lineNum;

        // collect all match positions on this line
        std::vector<std::pair<size_t,size_t>> positions; // start, len pairs
        for (size_t j = i; j < fr.matches.size() && fr.matches[j].lineNum == m.lineNum; j++) {
            positions.push_back({fr.matches[j].matchStart, fr.matches[j].matchLen});
        }

        if (cfg.showLineNumbers) {
            std::cout << colored(std::to_string(m.lineNum), CLR_GREEN) << ":";
        }

        // build highlighted line by inserting color codes around each match
        const std::string& line = *m.lineContent;
        std::string highlighted;
        size_t cur = 0;

        for (auto& [start, len] : positions) {
            if (start > cur) {
                highlighted += line.substr(cur, start - cur);
            }
            highlighted += colored(line.substr(start, len), CLR_RED CLR_BOLD);
            cur = start + len;
        }
        if (cur < line.size()) {
            highlighted += line.substr(cur);
        }

        std::cout << highlighted << "\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    SearchConfig cfg;
    std::vector<std::string> positional;

    // parse args manually - didnt want to pull in getopt or a library
    // for this, its not that many flags
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-i") {
            cfg.ignoreCase = true;
        } else if (arg == "-E") {
            cfg.useRegex = true;
        } else if (arg == "-c") {
            cfg.countOnly = true;
        } else if (arg == "-n") {
            cfg.showLineNumbers = false;
        } else if (arg == "--no-color") {
            colorEnabled = false;
        } else if (arg.rfind("--ext=", 0) == 0) {
            cfg.includeExts.push_back(arg.substr(6));
        } else if (arg.rfind("--depth=", 0) == 0) {
            try {
                std::string value = arg.substr(8);
                size_t consumed = 0;
                cfg.maxDepth = std::stoi(value, &consumed);
                if (consumed != value.size() || cfg.maxDepth < -1)
                    throw std::invalid_argument("invalid depth");
            } catch (...) {
                std::cerr << "invalid depth value\n";
                return 1;
            }
        } else if (arg[0] == '-') {
            std::cerr << "unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.size() != 2) {
        std::cerr << "error: need exactly <pattern> and <path>\n";
        printUsage(argv[0]);
        return 1;
    }

    cfg.pattern  = positional[0];
    cfg.rootPath = positional[1];

    if (!cfg.useRegex && cfg.pattern.empty()) {
        std::cerr << "error: empty literal pattern\n";
        return 1;
    }

    // validate regex early so we get a nice error before doing any work
    if (cfg.useRegex) {
        try {
            std::regex test(cfg.pattern);
            (void)test;
        } catch (const std::regex_error& e) {
            std::cerr << "invalid regex: " << e.what() << "\n";
            return 1;
        }
    }

    auto t0 = std::chrono::steady_clock::now();

    try {
        Searcher searcher(cfg);
        auto results = searcher.run();

        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();

        // print results
        for (const auto& fr : results) {
            printFileResult(fr, cfg);
        }

        // summary line at the bottom - similar to ripgrep style
        std::cout << "\n"
                  << colored("---", CLR_CYAN) << " "
                  << colored(std::to_string(searcher.totalMatches()), CLR_YELLOW)
                  << " match(es) in "
                  << colored(std::to_string(searcher.filesMatched()), CLR_YELLOW)
                  << "/"
                  << colored(std::to_string(searcher.filesScanned()), CLR_YELLOW)
                  << " file(s) "
                  << colored("(" + std::to_string(elapsed * 1000.0).substr(0, 5) + "ms)", CLR_GREEN)
                  << "\n";

        // An I/O failure is distinct from a completed search with no matches.
        if (searcher.hadErrors()) return 2;
        return (searcher.totalMatches() > 0) ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
