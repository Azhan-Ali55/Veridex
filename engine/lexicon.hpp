// ============================================================================
// lexicon.hpp -- the shared vocabulary between the indexer and the search side
// ============================================================================
//
// WHAT THIS CLASS OWNS
//   * the tokenizer rules           (static Lexicon::tokenize)
//   * the body-cleanup rules        (static Lexicon::cleanBody)
//   * the word -> wordID mapping    (built once, then saved to lexicon.txt)

#ifndef LEXICON_HPP
#define LEXICON_HPP

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

class Lexicon {
public:
    // Construction

    // A fresh, empty lexicon. IDs will be assigned 0,1,2,... as words arrive.
    Lexicon() = default;

    // Shared text-processing rules (used by indexer AND query code)
    // Tokenize one piece of text according to DESIGN.md:
    //   - lowercase only ASCII A-Z
    //   - a word is a maximal run of [a-z0-9]
    //   - every other byte (punctuation, whitespace, non-ASCII) is a separator
    //   - keep tokens whose length is in [2, 30]
    //   - no stopword removal
    // Because this is static and in the header, the search program can call
    // Lexicon::tokenize(query) and be guaranteed to get exactly the same
    // tokens the indexer produced for the same string.
    static std::vector<std::string> tokenize(const std::string& text);

    // Strip the Federal Register wrapper from a document body before indexing.
    //   - skip the GPO header up to the first "[[Page" marker (if present)
    //   - remove every "[[ ... ]]" page marker
    // If no "[[Page" marker exists the whole input is kept, which is what the
    // sample data and any future non-FR corpus will look like.
    static std::string cleanBody(const std::string& text);

    // Dictionary operations
    // Insert `word` if new and return its stable ID.
    // Returns the existing ID (unchanged) if the word was already present.
    // Precondition: `word` should already be tokenizer output (lowercase,
    // length 2..30). build() guarantees this; callers who bypass build()
    // are responsible for it.
    int addWord(const std::string& word);

    // Look up an existing word.
    // Returns the wordID, or -1 if the word is not in the lexicon.
    // Query code relies on -1 to mean "this query term cannot match anything".
    int getId(const std::string& word) const;

    // Reverse lookup: wordID -> word.
    // Throws std::out_of_range if `id` is not a valid wordID. Search code
    // that wants to print a matched term should trust IDs from its own index
    // and let this throw on a programming error.
    const std::string& getWord(int id) const;

    // Number of unique words currently in the lexicon.
    std::size_t size() const;

    // Persistence
    // Build the lexicon from docs.jsonl (one JSON object per line).
    // Reads the file line-by-line, so RAM usage is O(unique words), not
    // O(corpus size). Any line that is not a valid JSON object is skipped
    // and counted, not fatal. Clears any previous contents first, so calling
    // build() twice gives the same result as calling it once.
    // Fields indexed: title, abstract, and cleanBody(text). Missing or null
    // fields are treated as empty strings.
    bool build(const std::string& docsPath);

    // Write lexicon.txt in the format DESIGN.md specifies:
    //     wordID<TAB>word<newline>
    // IDs are written in order 0..N-1 so a later load() can reproduce the
    // exact same mapping. Returns false if the file cannot be opened or a
    // write error occurs.
    bool save(const std::string& path) const;

    // Load lexicon.txt back into memory.
    // Validates the file strictly: IDs must be 0,1,2,... in line order,
    // words must be non-empty and unique. On any violation the in-memory
    // lexicon is cleared and the method returns false, so a corrupted file
    // can never produce a half-loaded lexicon that disagrees with the index.
    bool load(const std::string& path);

private:
    // word -> wordID (fast lookup)
    std::unordered_map<std::string, int> ids_;
    // wordID -> word (vector index IS the wordID, so getWord is O(1))
    std::vector<std::string> words_;

    // Convenience wrapper: tokenize `text` and addWord() each token.
    void addText(const std::string& text);
};

#endif