#include "forward_index.hpp"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <docs.jsonl> <forward_index.bin>\n";
        return 1;
    }
    ForwardIndex fwd;
    if (!fwd.build(argv[1])) return 1;
    if (!fwd.save(argv[2])) {
        std::cerr << "Cannot write " << argv[2] << '\n';
        return 1;
    }
    std::cout << "Saved " << fwd.size() << " docs to " << argv[2] << '\n';
    return 0;
}