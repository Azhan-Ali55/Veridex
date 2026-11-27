
#include "forward_index.hpp"
#include "lexicon.hpp"

#include <iostream>
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <cassert>
#include <string>
#include <iterator>

int main() {

    std::cout << "===== FORWARD INDEX TEST =====\n\n";

    // ------------------------------------------------
    // TEST 1: Create sample JSONL dataset
    // ------------------------------------------------

    {
        std::ofstream file("test_docs.jsonl");

        file << R"({"document_number":"FR-001","title":"Federal Register","abstract":"New applications","publication_date":"2026-01-10","type":"Rule","agency_names":["Agency A"],"text":"[[Page 10]] Federal Register publishes new rules."})" << '\n';

        file << R"({"document_number":"FR-002","title":"Economic Policy","abstract":null,"publication_date":"2026-01-11","type":"Notice","agency_names":["Agency B","Agency C"],"text":"Economic development and rural innovation."})" << '\n';

        file << "INVALID JSON\n";

        file << R"({"document_number":"FR-003","title":"Rural Development","abstract":"Development plans","publication_date":"2026-01-12","type":"Notice","agency_names":[],"text":"Improving the rural economy."})" << '\n';
    }

    std::cout << "TEST 1: Dataset creation successful\n";

    // ------------------------------------------------
    // TEST 2: Build forward index
    // ------------------------------------------------

    ForwardIndex index;

    bool built = index.build("test_docs.jsonl");

    assert(built);

    std::cout << "TEST 2: Build successful\n";

    // ------------------------------------------------
    // TEST 3: Check document IDs and metadata
    // ------------------------------------------------

    const auto& doc0 = index.get(0);
    const auto& doc1 = index.get(1);
    const auto& doc2 = index.get(2);

    assert(doc0.documentNumber == "FR-001");
    assert(doc0.title == "Federal Register");

    assert(doc1.documentNumber == "FR-002");
    assert(doc1.title == "Economic Policy");
    assert(doc1.abstract.empty());

    assert(doc2.documentNumber == "FR-003");
    assert(doc2.title == "Rural Development");

    std::cout << "TEST 3: Document IDs and metadata correct\n";

    // ------------------------------------------------
    // TEST 4: Check body token counts
    // ------------------------------------------------

    std::string body1 =
        "Federal Register publishes new rules.";

    std::string body2 =
        "Economic development and rural innovation.";

    std::string body3 =
        "Improving the rural economy.";

    assert(doc0.bodyLength ==
           Lexicon::tokenize(body1).size());

    assert(doc1.bodyLength ==
           Lexicon::tokenize(body2).size());

    assert(doc2.bodyLength ==
           Lexicon::tokenize(body3).size());

    std::cout << "TEST 4: Body lengths correct\n";

    // ------------------------------------------------
    // TEST 5: Save binary index
    // ------------------------------------------------

    bool saved = index.save("test_forward_index.bin");

    assert(saved);

    std::cout << "TEST 5: Binary save successful\n";

    // ------------------------------------------------
    // TEST 6: Load binary index
    // ------------------------------------------------

    ForwardIndex loadedIndex;

    bool loaded = loadedIndex.load("test_forward_index.bin");

    assert(loaded);

    std::cout << "TEST 6: Binary load successful\n";

    // ------------------------------------------------
    // TEST 7: Verify loaded information
    // ------------------------------------------------

    assert(loadedIndex.get(0).title == "Federal Register");
    assert(loadedIndex.get(1).title == "Economic Policy");
    assert(loadedIndex.get(2).title == "Rural Development");

    assert(loadedIndex.get(0).bodyLength == doc0.bodyLength);

    assert(loadedIndex.get(1).agencies.size() == 2);

    assert(loadedIndex.get(2).agencies.empty());

    std::cout << "TEST 7: Loaded data matches original\n";

    // ------------------------------------------------
    // TEST 8: Invalid document ID
    // ------------------------------------------------

    bool exceptionCaught = false;

    try {
        loadedIndex.get(3);
    }
    catch (const std::out_of_range&) {
        exceptionCaught = true;
    }

    assert(exceptionCaught);

    std::cout << "TEST 8: Invalid document ID rejected\n";

    // ------------------------------------------------
    // TEST 9: Corrupted magic header
    // ------------------------------------------------

    {
        std::ifstream input(
            "test_forward_index.bin", std::ios::binary);

        std::string data(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());

        data[0] = 'X';

        std::ofstream output(
            "corrupted_index.bin", std::ios::binary);

        output.write(data.data(), data.size());
    }

    ForwardIndex corruptedIndex;

    assert(!corruptedIndex.load("corrupted_index.bin"));

    std::cout << "TEST 9: Corrupted header rejected\n";

    // ------------------------------------------------
    // TEST 10: Truncated binary file
    // ------------------------------------------------

    {
        std::ofstream output(
            "truncated_index.bin", std::ios::binary);

        output.write("FWDI", 4);
    }

    ForwardIndex truncatedIndex;

    assert(!truncatedIndex.load("truncated_index.bin"));

    std::cout << "TEST 10: Truncated file rejected\n";

    // ------------------------------------------------
    // CLEANUP
    // ------------------------------------------------

    std::remove("test_docs.jsonl");
    std::remove("test_forward_index.bin");
    std::remove("corrupted_index.bin");
    std::remove("truncated_index.bin");

    std::cout << "\n================================\n";
    std::cout << "ALL TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "================================\n";

    return 0;
}
