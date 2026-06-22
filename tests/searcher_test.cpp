#include "searcher.h"

#include <iostream>
#include <stdexcept>

int main() {
    SearchConfig cfg;
    cfg.pattern = "alpha";
    cfg.rootPath = ""; // first run reports an invalid path
    Searcher searcher(cfg);
    searcher.run();

    try {
        searcher.run();
    } catch (const std::logic_error&) {
        std::cout << "searcher one-shot test passed\n";
        return 0;
    }
    std::cerr << "second run was accepted\n";
    return 1;
}
