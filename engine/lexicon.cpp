#include "lexicon.hpp"
#include "json.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using json = nlohmann::json;

// Pull a string field out of a JSON object safely.
// Returns "" when:
//   * the key is missing
//   * the value is JSON null
//   * the value is not a JSON string
// This is exactly what we want for the Federal Register corpus, where
// `abstract` is frequently null and any field may be absent on a bad record.
std::string stringField(const json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        return std::string();
    }
    return it->get<std::string>();
}
} // namespace

// TOKENIZER -- the single source of truth for "what is a word"
// Rules (must match DESIGN.md and the search side):
//   1. Lowercase only ASCII A-Z. Non-ASCII bytes are left alone; they will
//      be treated as separators below, which is what we want for an English
//      corpus that may contain stray UTF-8.
//   2. A word is a maximal run of [a-z0-9]. Every other byte separates words.
//      So "co-loading" -> "co", "loading"; "$2,000,000" -> "000", "000";
//      "NVOCCs" -> "nvocc" (the trailing 's' is a letter, so it stays: "nvoccs").
//   3. Keep only tokens of length 2..30. This drops noise like "a", "7",
//      and pathological single tokens, while keeping real words intact.
//   4. No stopword removal: stopword filtering belongs at query time, not
//      here, so the index can still answer phrase queries that include them.
//
// The loop runs one past the end with a sentinel space so the final token
// is flushed without an extra branch after the loop.
std::vector<std::string> Lexicon::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    tokens.reserve(text.size() / 6 + 1); // rough guess, avoids early reallocs

    std::string current;

    for (std::size_t i = 0; i <= text.size(); ++i) {
        char c = (i < text.size()) ? text[i] : ' '; // sentinel flushes last token

        // Manual ASCII lowercase (cheaper than std::tolower and locale-free).
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }

        const bool isWordChar =
            (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');

        if (isWordChar) {
            current.push_back(c);
        } else {
            if (current.size() >= 2 && current.size() <= 30) {
                tokens.push_back(current);
            }
            current.clear();
        }
    }

    return tokens;
}

// BODY CLEANUP -- strip the Federal Register wrapper
// The downloaded body looks like:
//   "Federal Register, Volume 89 Issue 1 (...) [Pages 43-48] ... SUMMARY: ...
//    ... [[Page 44]] ... [[Page 45]] ... [FR Doc. ...]"
//
// We want to throw away the GPO header (everything up to the first "[[Page"
// marker) and then drop every "[[...]]" marker from what remains. If the
// input has no "[[Page" marker at all (e.g. a future corpus or a hand-written
// test), we keep the whole string -- nothing is a wrapper, so nothing is
// stripped.
//
// A malformed trailing "[[" with no closing "]]" is dropped along with the
// rest of the input; the document is already truncated at that point and
// keeping half a marker would just inject junk tokens.
std::string Lexicon::cleanBody(const std::string& text) {
    const std::size_t start = text.find("[[Page");
    const std::string source =
        (start == std::string::npos) ? text : text.substr(start);

    std::string cleaned;
    cleaned.reserve(source.size());

    std::size_t i = 0;
    while (i < source.size()) {
        if (source.compare(i, 2, "[[") == 0) {
            const std::size_t end = source.find("]]", i + 2);
            if (end == std::string::npos) {
                // Malformed: stop here rather than emit half a marker.
                break;
            }
            i = end + 2; // skip the whole "[[...]]" chunk
        } else {
            cleaned.push_back(source[i]);
            ++i;
        }
    }

    return cleaned;
}

// DICTIONARY OPERATIONS
int Lexicon::addWord(const std::string& word) {
    // Fast path: word already known. Never renumber an existing word.
    auto it = ids_.find(word);
    if (it != ids_.end()) {
        return it->second;
    }

    // New word: its ID is the current size, so IDs are contiguous 0..N-1
    // in first-appearance order. This invariant is what makes save()/load()
    // round-trip exactly.
    const int wordID = static_cast<int>(words_.size());

    ids_.emplace(word, wordID);
    words_.push_back(word);
    return wordID;
}

int Lexicon::getId(const std::string& word) const {
    auto it = ids_.find(word);
    return (it == ids_.end()) ? -1 : it->second;
}

const std::string& Lexicon::getWord(int id) const {
    // A bad ID here is a bug in the caller (e.g. an index record whose ID
    // does not match this lexicon). Fail loudly rather than read out of bounds.
    if (id < 0 || static_cast<std::size_t>(id) >= words_.size()) {
        throw std::out_of_range("Lexicon::getWord: invalid word ID");
    }
    return words_[static_cast<std::size_t>(id)];
}

std::size_t Lexicon::size() const {
    return words_.size();
}

void Lexicon::addText(const std::string& text) {
    for (const std::string& word : tokenize(text)) {
        addWord(word);
    }
}

// BUILD -- read docs.jsonl line by line
bool Lexicon::build(const std::string& docsPath) {
    std::ifstream input(docsPath);
    if (!input.is_open()) {
        std::cerr << "Error: cannot open " << docsPath << '\n';
        return false;
    }

    // Make build() idempotent: calling it twice must not accumulate state
    // from a previous corpus, which would silently shift wordIDs.
    ids_.clear();
    words_.clear();

    const auto startTime = std::chrono::steady_clock::now();

    std::size_t documents = 0;
    std::size_t badLines = 0;
    std::string line;

    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        // `false` -> do not throw on parse error; return a discarded value.
        const json document = json::parse(line, nullptr, false);
        if (document.is_discarded() || !document.is_object()) {
            ++badLines;
            continue;
        }

        // All three text fields are indexed. Any of them may be missing or
        // null (abstract frequently is); stringField() collapses that to "".
        addText(stringField(document, "title"));
        addText(stringField(document, "abstract"));
        addText(cleanBody(stringField(document, "text")));

        ++documents;
        if (documents % 5000 == 0) {
            std::cout << "  " << documents << " docs, "
                      << words_.size() << " unique words\n";
        }
    }

    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startTime).count();

    std::cout << "Done: " << documents << " docs ("
              << badLines << " bad lines), "
              << words_.size() << " unique words, "
              << seconds << " s\n";
    return true;
}

// SAVE / LOAD -- the lexicon.txt on-disk contract
bool Lexicon::save(const std::string& path) const {
    std::ofstream output(path);
    if (!output.is_open()) {
        return false;
    }

    for (std::size_t i = 0; i < words_.size(); ++i) {
        output << i << '\t' << words_[i] << '\n';
    }

    // Catch disk-full / permission errors that would otherwise look like success.
    return output.good();
}

bool Lexicon::load(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }

    ids_.clear();
    words_.clear();

    std::string line;
    std::size_t expectedID = 0;

    while (std::getline(input, line)) {
        // A lexicon written on Windows has CRLF line endings; getline() only
        // removes the '\n', so drop the leftover '\r' before parsing.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Tolerate a trailing blank line (some editors add one). It carries
        // no record, so it is not a corruption.
        if (line.empty()) {
            continue;
        }

        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            ids_.clear();
            words_.clear();
            return false;
        }

        const std::string idString = line.substr(0, tab);
        const std::string word = line.substr(tab + 1);

        char* end = nullptr;
        const long id = std::strtol(idString.c_str(), &end, 10);

        // Every record must be well-formed and IDs must be 0,1,2,... in
        // line order, so a reloaded lexicon is byte-for-byte the same map
        // the indexer saw. Anything else is treated as corruption.
        const bool bad =
            end == idString.c_str() ||       // no digits at all
            *end != '\0' ||                  // trailing garbage after the digits
            id < 0 ||                        // negative
            static_cast<std::size_t>(id) != expectedID || // out of order / gap
            word.empty() ||                  // empty word
            ids_.find(word) != ids_.end();   // duplicate word

        if (bad) {
            ids_.clear();
            words_.clear();
            return false;
        }

        words_.push_back(word);
        ids_.emplace(word, static_cast<int>(id));
        ++expectedID;
    }

    return input.eof();
}