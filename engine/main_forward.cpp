// Usage:
//   forward_index <docs.jsonl> <lexicon.txt> <forward_index.bin>
//
// The lexicon must have been built from the SAME docs.jsonl (see
// main_lexicon.cpp); otherwise some tokens would have no wordID.
//
// Exit codes: 0 success, 1 usage / I/O error, 2 lexicon out of sync.

#include "forward_index.hpp"
#include "lexicon.hpp"

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <docs.jsonl> <lexicon.txt> <forward_index.bin>\n";
        return 1;
    }

    Lexicon lexicon;
    if (!lexicon.load(argv[2])) {
        std::cerr << "Cannot load lexicon (missing or corrupt): " << argv[2] << '\n';
        return 1;
    }
    std::cout << "Loaded lexicon: " << lexicon.size() << " words\n";

    ForwardIndex index;
    if (!index.build(argv[1], lexicon)) {
        return 1;
    }

    // Tokens without a wordID were dropped, so document lengths and term
    // counts would be wrong. Refuse to write an index that disagrees with the
    // lexicon rather than let the mismatch surface later as bad search results.
    if (index.stats().lexiconMisses != 0) {
        std::cerr << "Error: " << index.stats().lexiconMisses
                  << " tokens are missing from the lexicon. It was not built from "
                  << argv[1] << "; rebuild the lexicon and try again.\n";
        return 2;
    }

    if (!index.save(argv[3])) {
        std::cerr << "Cannot write " << argv[3] << '\n';
        return 1;
    }
    std::cout << "Saved " << index.size() << " docs to " << argv[3] << '\n';
    return 0;
}