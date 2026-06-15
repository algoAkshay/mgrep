#pragma once

#include <string>
#include <vector>
#include <memory>

// represents one match within a file
struct Match {
    int lineNum;
    std::shared_ptr<const std::string> lineContent; // shared by matches on one line
    size_t matchStart;  // byte offset into the line where pattern starts
    size_t matchLen;
};

// all matches from a single file
struct FileResult {
    std::string filepath;
    std::vector<Match> matches;

    bool hasMatches() const { return !matches.empty(); }
};

// config the user passes in via CLI
struct SearchConfig {
    std::string pattern;
    std::string rootPath;
    bool ignoreCase       = false;
    bool useRegex         = false;
    bool countOnly        = false;  // like grep -c
    bool showLineNumbers  = true;
    int  maxDepth         = -1;     // -1 = unlimited
    std::vector<std::string> includeExts;  // e.g. {".cpp", ".h"}

    // TODO: add -l flag (only filenames), maybe someday
};
