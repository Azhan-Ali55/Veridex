#include <iostream>
#include "inverted_index.hpp"
#include "forward_index.hpp"
#include "lexicon.hpp"

using namespace std;

int main(int argc, char* argv[]) {

    if (argc != 5) {

        cerr << "Usage: " << argv[0]
            << " <forward_index.bin>"
            << " <lexicon.txt>"
            << " <docs.jsonl>"
            << " <output.bin>\n";

        return 1;
    }

    Lexicon lexicon;

    // Load the existing lexicon.
    if (!lexicon.load(argv[2])) {

        cerr << "Could not load lexicon.\n";

        return 1;
    }

    ForwardIndex forward;

    // Load the forward index created by our teammate.
    if (!forward.load(argv[1])) {

        cerr << "Could not load forward index.\n";

        return 1;
    }

    InvertedIndex index;

    // Reverse the forward index and recover positions.
    if (!index.build(forward, lexicon, argv[3])) {

        cerr << "Could not build inverted index.\n";

        return 1;
    }

    // Save the index for future searches.
    if (!index.save(argv[4])) {

        cerr << "Could not save inverted index.\n";

        return 1;
    }

    cout << "Documents indexed: "
        << index.documentCount() << '\n';

    cout << "Posting lists: "
        << index.size() << '\n';

    cout << "Saved inverted index to "
        << argv[4] << '\n';

    return 0;
}