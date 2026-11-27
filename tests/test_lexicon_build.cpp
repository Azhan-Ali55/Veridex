
#include <iostream>
#include <string>
#include "../engine/lexicon.hpp"

using namespace std;

int main(int argc, char* argv[]) {

    if (argc != 3) {
        cout << "Usage: test_lexicon_build <docs.jsonl> <lexicon.txt>\n";
        return 1;
    }

    string datasetPath = argv[1];
    string lexiconPath = argv[2];

    // Test 1: Build lexicon from dataset
    cout << "===== TEST 1: BUILD =====\n";

    Lexicon original;

    if (!original.build(datasetPath)) {
        cout << "Failed to build lexicon.\n";
        return 1;
    }

    cout << "Build successful.\n";
    cout << "Unique words: " << original.size() << "\n";

    // Display first 10 words
    cout << "\nFirst 10 lexicon entries:\n";

    for (int i = 0; i < 10 && i < original.size(); i++) {
        cout << i << " -> " << original.getWord(i) << "\n";
    }

    // Test 2: Save lexicon
    cout << "\n===== TEST 2: SAVE =====\n";

    if (!original.save(lexiconPath)) {
        cout << "Failed to save lexicon.\n";
        return 1;
    }

    cout << "Save successful.\n";

    // Test 3: Load lexicon
    cout << "\n===== TEST 3: LOAD =====\n";

    Lexicon loaded;

    if (!loaded.load(lexiconPath)) {
        cout << "Failed to load lexicon.\n";
        return 1;
    }

    cout << "Load successful.\n";
    cout << "Loaded words: " << loaded.size() << "\n";

    // Test 4: Compare original and loaded lexicons
    cout << "\n===== TEST 4: VERIFICATION =====\n";

    bool identical = (original.size() == loaded.size());

    if (identical) {
        for (int i = 0; i < original.size(); i++) {
            if (original.getWord(i) != loaded.getWord(i) ||
                original.getId(original.getWord(i)) !=
                loaded.getId(original.getWord(i))) {

                identical = false;
                break;
            }
        }
    }

    if (identical) {
        cout << "PASS: Original and loaded lexicons are identical.\n";
    } else {
        cout << "FAIL: Original and loaded lexicons differ.\n";
        return 1;
    }

    cout << "\nAll tests completed.\n";

    return 0;
}
