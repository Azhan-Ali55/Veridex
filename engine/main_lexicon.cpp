#include "lexicon.hpp"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <docs.jsonl> <lexicon.txt>\n";
        return 1;
    }
    Lexicon lex;
    if (!lex.build(argv[1])) return 1;
    if (!lex.save(argv[2])) {
        std::cerr << "Cannot write " << argv[2] << '\n';
        return 1;
    }
    std::cout << "Saved " << lex.size() << " words to " << argv[2] << '\n';
    return 0;
}