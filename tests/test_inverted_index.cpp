#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cassert>
#include <cstdio>

#include "inverted_index.hpp"
#include "forward_index.hpp"
#include "lexicon.hpp"
#include "json_util.hpp"

using namespace std;

int main(int argc, char* argv[]) {

    if (argc != 5) {

        cout << "Incorrect number of arguments.\n";
        return 1;
    }

    ForwardIndex forward;
    Lexicon lexicon;
    InvertedIndex index;

    // Load all three indexes.
    assert(forward.load(argv[1]));
    assert(lexicon.load(argv[2]));
    assert(index.load(argv[4]));

    // Both indexes should have the same documents.
    assert(index.documentCount() == forward.size());
    assert(index.size() == lexicon.size());

    size_t totalPostings = 0;
    uint64_t totalPositions = 0;

    // Compare each posting with the forward index.
    for (size_t wordID = 0; wordID < index.size(); wordID++) {

        const auto& list = index.get(wordID);

        for (const auto& posting : list.postings) {

            assert(
                forward.termFrequency(
                    posting.docID, wordID
                ) == posting.tf
            );

            totalPostings++;
            totalPositions += posting.tf;
        }
    }

    assert(totalPostings == forward.entryCount());

    cout << "Term frequency test passed.\n";

    // Keep track of the postings and positions we check.
    vector<size_t> cursors(index.size(), 0);

    vector<uint32_t> counts(index.size(), 0);
    vector<uint32_t> titles(index.size(), 0);
    vector<uint32_t> abstracts(index.size(), 0);

    ifstream input(argv[3]);

    assert(input);

    string line;
    uint32_t docID = 0;
    uint64_t checkedTokens = 0;

    // Read the original documents to verify word positions.
    while (getline(input, line)) {

        const auto doc =
            nlohmann::json::parse(line, nullptr, false);

        if (doc.is_discarded() || !doc.is_object()) {
            continue;
        }

        assert(docID < forward.size());

        const string fields[] = {

            json_util::stringField(doc, "title"),

            json_util::stringField(doc, "abstract"),

            Lexicon::cleanBody(
                json_util::stringField(doc, "text")
            )
        };

        uint32_t pos = 0;

        // Check the title, abstract and body separately.
        for (int field = 0; field < 3; field++) {

            for (const auto& word :
                Lexicon::tokenize(fields[field])) {

                int id = lexicon.getId(word);

                assert(id >= 0);

                const auto& list = index.get(id);

                const auto& posting =
                    list.postings.at(cursors[id]);

                assert(posting.docID == docID);

                assert(counts[id] < posting.tf);

                // The stored position should match
                // the actual position in the document.
                assert(
                    list.positions.at(
                        posting.positionOffset + counts[id]
                    ) == pos
                );

                counts[id]++;
                pos++;

                if (field == 0) {
                    titles[id]++;
                }

                if (field == 1) {
                    abstracts[id]++;
                }

                checkedTokens++;
            }
        }

        assert(pos == forward.docLength(docID));

        // Check the frequencies before moving
        // to the next document.
        for (const auto& entry : forward.get(docID)) {

            uint32_t id = entry.wordID;

            const auto& posting =
                index.get(id).postings.at(cursors[id]);

            assert(counts[id] == posting.tf);

            assert(titles[id] == posting.titleTF);

            assert(abstracts[id] == posting.abstractTF);

            cursors[id]++;

            counts[id] = 0;
            titles[id] = 0;
            abstracts[id] = 0;
        }

        docID++;
    }

    assert(docID == forward.size());

    assert(checkedTokens == totalPositions);

    // Make sure every posting has been checked.
    for (size_t id = 0; id < index.size(); id++) {

        assert(
            cursors[id] ==
            index.get(id).postings.size()
        );
    }

    cout << "Word position test passed.\n";
    cout << "Field frequency test passed.\n";

    // Create an incomplete file to test error handling.
    string corruptFile = string(argv[4]) + ".test-bad";

    {
        ofstream out(corruptFile, ios::binary);

        out.write("INVIDX02", 8);

        assert(out);
    }

    // Loading a damaged index should fail.
    assert(!index.load(corruptFile));

    // The original index should still be available.
    assert(index.documentCount() == forward.size());

    remove(corruptFile.c_str());

    cout << "Corrupted file test passed.\n";

    cout << "\nAll tests passed successfully!\n";

    cout << "Documents: "
        << index.documentCount() << endl;

    cout << "Unique words: "
        << index.size() << endl;

    cout << "Verified postings: "
        << totalPostings << endl;

    cout << "Verified positions: "
        << totalPositions << endl;

    return 0;
}