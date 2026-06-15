#include "searcher.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>

static char lowerByte(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// how many threads to spin up - using hardware_concurrency as baseline
// on most machines this is # of logical cores. I/O bound tasks usually
// benefit from going a bit higher than core count but lets keep it simple
static size_t chooseThreadCount() {
    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4; // fallback if the OS doesnt tell us
    return static_cast<size_t>(hw);
}

Searcher::Searcher(SearchConfig cfg) : config(std::move(cfg)) {
    pool = std::make_unique<ThreadPool>(chooseThreadCount());

    if (config.useRegex) {
        auto flags = std::regex_constants::ECMAScript | std::regex_constants::optimize;
        if (config.ignoreCase)
            flags |= std::regex_constants::icase;
        pattern = std::regex(config.pattern, flags);
    }
}

std::vector<FileResult> Searcher::run() {
    if (started.exchange(true)) throw std::logic_error("searcher can only run once");
    fs::path root(config.rootPath);
    std::error_code ec;

    bool exists = fs::exists(root, ec);
    if (ec) {
        reportError(root, ec.message());
        return {};
    }
    if (!exists) {
        reportError(root, "path does not exist");
        return {};
    }

    bool regular = fs::is_regular_file(root, ec);
    if (ec) {
        reportError(root, ec.message());
        return {};
    }
    if (regular) {
        // user passed a single file directly, just search that
        if (extensionAllowed(root)) searchFile(root);
    } else {
        bool directory = fs::is_directory(root, ec);
        if (ec) {
            reportError(root, ec.message());
            return {};
        }
        if (!directory) {
            reportError(root, "not a regular file or directory");
            return {};
        }
        walkDirectory(root, 0);
    }

    pool->waitAll();

    // sort by filepath so output is deterministic
    // (threads finish in random order obviously)
    std::sort(results.begin(), results.end(), [](const FileResult& a, const FileResult& b){
        return a.filepath < b.filepath;
    });

    return results;
}

void Searcher::walkDirectory(const fs::path& dir, int depth) {
    if (config.maxDepth >= 0 && depth > config.maxDepth)
        return;

    std::error_code ec;
    fs::directory_iterator it(dir, ec), end;
    if (ec) {
        reportError(dir, ec.message());
        return;
    }
    while (it != end) {
        fs::directory_entry entry = *it;
        fs::file_status status = entry.symlink_status(ec);
        if (ec) {
            reportError(entry.path(), ec.message());
        } else if (fs::is_symlink(status)) {
            // skip symlinks to avoid cycles
        } else if (fs::is_directory(status)) {
            walkDirectory(entry.path(), depth + 1);
        } else if (fs::is_regular_file(status) && extensionAllowed(entry.path())) {
            // capture path by value - important, iterator will advance
            fs::path p = entry.path();
            pool->enqueue([this, p]() {
                searchFile(p);
            });
        }
        it.increment(ec);
        if (ec) {
            reportError(dir, ec.message());
            break;
        }
    }
}

void Searcher::searchFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        reportError(path, "cannot open file");
        return;
    }

    numFilesScanned++;

    // Check file bytes rather than only the first line.
    std::array<char, 512> header{};
    file.read(header.data(), header.size());
    size_t headerSize = static_cast<size_t>(file.gcount());
    if (file.bad()) {
        reportError(path, "cannot read file");
        return;
    }
    if (std::find(header.begin(), header.begin() + headerSize, '\0') !=
        header.begin() + headerSize) return;
    file.clear();
    file.seekg(0);
    if (!file) {
        reportError(path, "cannot rewind file");
        return;
    }

    FileResult result;
    result.filepath = path.string();

    std::string line;
    int lineNum = 0;

    while (std::getline(file, line)) {
        lineNum++;

        auto matches = findMatches(line, lineNum);
        for (auto& m : matches) {
            result.matches.push_back(std::move(m));
            numTotalMatches++;
        }
    }
    if (!file.eof()) reportError(path, "cannot read file");
    if (result.hasMatches()) {
        numFilesMatched++;
        std::lock_guard<std::mutex> lock(resultsMutex);
        results.push_back(std::move(result));
    }
}

// returns all matches on this line (there can be multiple)
std::vector<Match> Searcher::findMatches(const std::string& line, int lineNum) {
    std::vector<Match> found;
    std::shared_ptr<const std::string> sharedLine;

    if (!config.useRegex && config.pattern.empty()) return found;

    if (config.useRegex) {
        // regex search path
        auto begin = std::sregex_iterator(line.begin(), line.end(), pattern);
        auto end   = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            if (!sharedLine) sharedLine = std::make_shared<const std::string>(line);
            Match m;
            m.lineNum     = lineNum;
            m.lineContent = sharedLine;
            m.matchStart  = it->position();
            m.matchLen    = it->length();
            found.push_back(std::move(m));
        }
    } else {
        // plain string search - faster than regex for simple patterns
        std::string haystack = line;
        std::string needle   = config.pattern;

        if (config.ignoreCase) {
            // lowercase both for comparison
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), lowerByte);
            std::transform(needle.begin(),   needle.end(),   needle.begin(),   lowerByte);
        }

        size_t pos = 0;
        while ((pos = haystack.find(needle, pos)) != std::string::npos) {
            if (!sharedLine) sharedLine = std::make_shared<const std::string>(line);
            Match m;
            m.lineNum     = lineNum;
            m.lineContent = sharedLine; // original line, not lowercased
            m.matchStart  = pos;
            m.matchLen    = needle.size();
            found.push_back(std::move(m));
            pos += needle.size(); // advance past this match
        }
    }

    return found;
}

bool Searcher::extensionAllowed(const fs::path& p) const {
    if (config.includeExts.empty()) return true; // no filter = allow everything

    std::string ext = p.extension().string();
    // case insensitive ext check
    std::transform(ext.begin(), ext.end(), ext.begin(), lowerByte);

    for (const auto& allowed : config.includeExts) {
        std::string a = allowed;
        std::transform(a.begin(), a.end(), a.begin(), lowerByte);
        if (ext == a) return true;
    }
    return false;
}

void Searcher::reportError(const fs::path& p, const std::string& message) {
    searchError = true;
    std::lock_guard<std::mutex> lock(errorMutex);
    std::cerr << "error: " << p << ": " << message << "\n";
}
