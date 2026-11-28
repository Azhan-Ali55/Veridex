// Uses its own CHECK macro rather than assert() so the checks still run in
// release builds (-DNDEBUG). Scratch files are created in the working
// directory and removed afterwards. Exit code 0 means every check passed.

#include "forward_index.hpp"
#include "lexicon.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::cerr << "  FAILED line " << __LINE__ << ": " #cond << '\n'; \
        }                                                                    \
    } while (0)

const char* kDocs     = "t_docs.jsonl";
const char* kIndex    = "t_index.bin";
const char* kCorrupt  = "t_corrupt.bin";

// docs.jsonl used by most tests. Expected docIDs:
//   0 : "Alpha beta alpha" / abstract null / body "gamma alpha"
//   (bad line skipped -- must NOT consume a docID)
//   1 : title only, "Beta beta beta"
//   2 : no indexable text at all (empty document)
//   3 : header + [[Page marker, so cleanBody strips the header
void writeCorpus() {
    std::ofstream f(kDocs);
    f << "{\"title\":\"Alpha beta alpha\",\"abstract\":null,\"text\":\"gamma alpha\"}\n";
    f << "this line is not json\n";
    f << "\n";   // blank line, also skipped
    f << "{\"title\":\"Beta beta beta\"}\n";
    f << "{\"title\":\"\",\"abstract\":\"\",\"text\":\"\"}\n";
    f << "{\"title\":\"delta\",\"text\":\"HEADER JUNK [[Page 1]] epsilon delta\"}\n";
}

std::string readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

void writeFile(const char* path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

void setU32(std::string& data, std::size_t offset, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        data[offset + i] = static_cast<char>((v >> (8 * i)) & 0xFF);
    }
}

// Does loading `data` fail, AND leave an existing index untouched?
bool rejectsAndKeeps(const std::string& data, ForwardIndex& existing,
                     std::size_t expectedSize) {
    writeFile(kCorrupt, data);
    const bool rejected = !existing.load(kCorrupt);
    return rejected && existing.size() == expectedSize;
}

} // namespace

int main() {
    std::cout << "===== FORWARD INDEX TESTS =====\n";
    writeCorpus();

    Lexicon lexicon;
    CHECK(lexicon.build(kDocs));

    std::cout << "TEST 1: build, docID contract, statistics\n";
    ForwardIndex fwd;
    CHECK(fwd.size() == 0);                       // empty before build
    CHECK(fwd.build(kDocs, lexicon));
    CHECK(fwd.size() == 4);                       // bad + blank lines consumed no docID
    CHECK(fwd.stats().documents == 4);
    CHECK(fwd.stats().badLines == 1);
    CHECK(fwd.stats().lexiconMisses == 0);

    std::cout << "TEST 2: term frequencies and document length\n";
    const int alpha = lexicon.getId("alpha");
    const int beta  = lexicon.getId("beta");
    const int gamma = lexicon.getId("gamma");
    CHECK(alpha >= 0 && beta >= 0 && gamma >= 0);
    CHECK(fwd.termFrequency(0, alpha) == 3);      // 2 in title + 1 in body
    CHECK(fwd.termFrequency(0, beta) == 1);
    CHECK(fwd.termFrequency(0, gamma) == 1);
    CHECK(fwd.docLength(0) == 5);
    CHECK(fwd.termFrequency(1, beta) == 3);
    CHECK(fwd.termFrequency(1, alpha) == 0);      // absent word -> 0
    CHECK(fwd.docLength(1) == 3);
    CHECK(fwd.docLength(2) == 0);                 // empty document is valid
    CHECK(fwd.get(2).empty());

    std::cout << "TEST 3: cleanBody applied (header before [[Page is dropped)\n";
    CHECK(lexicon.getId("junk") == -1);
    CHECK(fwd.docLength(3) == 3);                 // delta (title) + epsilon + delta

    std::cout << "TEST 4: entries sorted ascending by wordID, tf >= 1\n";
    for (std::uint32_t d = 0; d < fwd.size(); ++d) {
        const ForwardIndex::TermList t = fwd.get(d);
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < t.size(); ++i) {
            CHECK(t[i].tf >= 1);
            if (i > 0) CHECK(t[i].wordID > t[i - 1].wordID);
            sum += t[i].tf;
        }
        CHECK(sum == fwd.docLength(d));
    }
    CHECK(fwd.entryCount() == 3 + 1 + 0 + 2);     // distinct words per doc

    std::cout << "TEST 5: out-of-range docID throws\n";
    bool threw = false;
    try { fwd.get(4); } catch (const std::out_of_range&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { fwd.docLength(99); } catch (const std::out_of_range&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { fwd.termFrequency(4, 0); } catch (const std::out_of_range&) { threw = true; }
    CHECK(threw);

    std::cout << "TEST 6: save / load round trip (and overwrite)\n";
    CHECK(fwd.save(kIndex));
    CHECK(fwd.save(kIndex));                      // second save over existing file
    ForwardIndex loaded;
    CHECK(loaded.load(kIndex));
    CHECK(loaded.size() == fwd.size());
    CHECK(loaded.entryCount() == fwd.entryCount());
    bool same = true;
    for (std::uint32_t d = 0; d < fwd.size(); ++d) {
        const ForwardIndex::TermList a = fwd.get(d);
        const ForwardIndex::TermList b = loaded.get(d);
        if (a.size() != b.size() || fwd.docLength(d) != loaded.docLength(d)) { same = false; continue; }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i].wordID != b[i].wordID || a[i].tf != b[i].tf) same = false;
        }
    }
    CHECK(same);
    CHECK(readFile("t_index.bin.tmp").empty());   // temp file was cleaned up

    std::cout << "TEST 7: empty index round trip\n";
    ForwardIndex empty, emptyLoaded;
    CHECK(empty.save(kCorrupt));
    CHECK(emptyLoaded.load(kCorrupt));
    CHECK(emptyLoaded.size() == 0 && emptyLoaded.entryCount() == 0);

    std::cout << "TEST 8: missing file\n";
    ForwardIndex other;
    CHECK(!other.load("no_such_file.bin"));
    CHECK(!fwd.build("no_such_file.jsonl", lexicon));
    CHECK(fwd.size() == 4);                       // failed build keeps old contents

    std::cout << "TEST 9: corrupt files are rejected and existing data kept\n";
    const std::string good = readFile(kIndex);
    const std::size_t entriesStart = 20 + 8 * 4;  // header + doc table (4 docs)
    std::string bad;

    bad = good; bad[0] = 'X';                              // bad magic
    CHECK(rejectsAndKeeps(bad, fwd, 4));

    bad = good.substr(0, good.size() - 1);                 // truncated by one byte
    CHECK(rejectsAndKeeps(bad, fwd, 4));
    bad = good.substr(0, 10);                              // truncated inside header
    CHECK(rejectsAndKeeps(bad, fwd, 4));

    bad = good + std::string(1, '\0');                     // trailing byte
    CHECK(rejectsAndKeeps(bad, fwd, 4));

    bad = good; setU32(bad, 8, 0xFFFFFFFFu);               // absurd docCount
    CHECK(rejectsAndKeeps(bad, fwd, 4));                   // must not throw bad_alloc

    bad = good; setU32(bad, 12, 0xFFFFFFFFu); setU32(bad, 16, 0xFFFFFFFFu);  // absurd entryCount
    CHECK(rejectsAndKeeps(bad, fwd, 4));

    bad = good; setU32(bad, 20, 0xFFFFFFF0u);              // doc 0 termCount overruns
    CHECK(rejectsAndKeeps(bad, fwd, 4));

    bad = good; setU32(bad, 24, 999);                      // doc 0 length != sum(tf)
    CHECK(rejectsAndKeeps(bad, fwd, 4));

    // Make doc 0's first two wordIDs non-ascending by copying entry 0's
    // wordID over entry 1's.
    bad = good;
    for (int i = 0; i < 4; ++i) bad[entriesStart + 8 + i] = bad[entriesStart + i];
    CHECK(rejectsAndKeeps(bad, fwd, 4));

    bad = good; setU32(bad, entriesStart + 4, 0);          // tf == 0
    CHECK(rejectsAndKeeps(bad, fwd, 4));

    std::cout << "TEST 10: stale lexicon is detected\n";
    Lexicon stale;
    stale.addWord("alpha");                       // knows only one word
    ForwardIndex staleFwd;
    CHECK(staleFwd.build(kDocs, stale));
    CHECK(staleFwd.stats().lexiconMisses > 0);

    std::cout << "TEST 11: build twice is idempotent\n";
    CHECK(fwd.build(kDocs, lexicon));
    CHECK(fwd.size() == 4 && fwd.entryCount() == 6);

    std::remove(kDocs);
    std::remove(kIndex);
    std::remove(kCorrupt);

    std::cout << "\n" << g_checks << " checks, " << g_failures << " failed\n";
    if (g_failures == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    return 1;
}