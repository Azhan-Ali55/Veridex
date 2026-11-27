
#include <iostream>
#include <vector>
#include <string>
#include "../engine/lexicon.hpp"

using namespace std;

int main() {

    Lexicon lexicon;

    // Test 1: Tokenization
    cout << "===== TEST 1: TOKENIZATION =====\n";

    string text = "Hello WORLD! Searching 123 C++ and Data-Structures.";

    vector<string> tokens = Lexicon::tokenize(text);

    for (const string& word : tokens) {
        cout << word << " ";
    }

    cout << "\n\n";

    // Test 2: Adding words and duplicate handling
    cout << "===== TEST 2: ADD WORDS =====\n";

    int id1 = lexicon.addWord("search");
    int id2 = lexicon.addWord("engine");
    int id3 = lexicon.addWord("search");

    cout << "search ID: " << id1 << endl;
    cout << "engine ID: " << id2 << endl;
    cout << "search again ID: " << id3 << endl;

    // Test 3: Getting IDs
    cout << "\n===== TEST 3: GET ID =====\n";

    cout << "engine: " << lexicon.getId("engine") << endl;
    cout << "unknown: " << lexicon.getId("unknown") << endl;

    // Test 4: Getting words
    cout << "\n===== TEST 4: GET WORD =====\n";

    cout << "ID 0: " << lexicon.getWord(0) << endl;
    cout << "ID 1: " << lexicon.getWord(1) << endl;

    // Test 5: Lexicon size
    cout << "\n===== TEST 5: SIZE =====\n";

    cout << "Unique words: " << lexicon.size() << endl;

    // Test 6: Saving
    cout << "\n===== TEST 6: SAVE =====\n";

    if (lexicon.save("test_lexicon.txt")) {
        cout << "Saved successfully.\n";
    }
    else {
        cout << "Saving failed.\n";
    }

    return 0;
}
