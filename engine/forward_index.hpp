// WHAT THIS MODULE OWNS
//   * the docID contract: docID 0 is the first valid line of docs.jsonl,
//     docID 1 the next, etc. Invalid JSON lines are skipped and do NOT
//     consume a docID. The inverted indexer MUST use the same rule, which is
//     why the assignment lives here and is reused, not re-implemented.
//   * the forward index proper: for each docID, its document_number, title,
//     abstract, publication date, agency names, document type, and body
//     length (in tokens, after the shared Lexicon tokenizer runs).
//
// ON-DISK FORMAT (forward_index.bin)
//   Little-endian binary, one record per document, no padding, in docID
//   order. Loading is one fread into a vector; there is no per-record parsing
//   loop, which keeps startup cheap even at 60k documents. The format is
//   versioned with a magic string so a stale file fails loudly instead of
//   producing garbage.
//
//   magic        : 8 bytes  "FWDIDX01"
//   docCount     : uint32
//   record[0..docCount-1], each:
//       docNumber    : length-prefixed string (uint32 len + bytes)
//       title        : length-prefixed string
//       abstract     : length-prefixed string
//       pubDate      : length-prefixed string  (ISO "YYYY-MM-DD")
//       docType      : length-prefixed string  ("Notice", "Rule", ...)
//       agencies     : uint16 count, then count length-prefixed strings
//       bodyLength   : uint32  (number of tokens, post-cleanBody)
//
//   Length-prefixed strings keep the file compact (no NUL-escaping issues,
//   no JSON overhead, no double-escaping of quotes inside abstracts) and
//   make the reader a straight forward cursor walk.

#ifndef FORWARD_INDEX_HPP
#define FORWARD_INDEX_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class ForwardIndex {
public:
    // One document's stored fields. Kept as a plain struct so the indexer and
    // search program can read/write individual fields without getter noise.
    // The strings are owned; body text is NOT stored here (it lives in the
    // posting lists of the inverted index and, if needed for snippets, can be
    // re-read from the original texts/ directory).
    struct DocRecord {
        std::string documentNumber;     // e.g. "2023-26792"
        std::string title;
        std::string abstract;
        std::string publicationDate;    // ISO "YYYY-MM-DD"
        std::string docType;            // "Notice", "Rule", "Proposed Rule", ...
        std::vector<std::string> agencies;
        std::uint32_t bodyLength = 0;   // token count of cleanBody(text)
    };

    ForwardIndex() = default;

    // Construction from docs.jsonl
    // Build the forward index from docs.jsonl, assigning docIDs 0,1,2,...
    // to valid JSON lines in file order. Clears any previous contents so
    // calling build() twice is idempotent.
    //
    // Uses the SAME tokenizer as the Lexicon (Lexicon::tokenize) and the
    // SAME body cleanup (Lexicon::cleanBody) so bodyLength matches the
    // number of tokens the inverted indexer will actually emit for this doc.
    //
    // Returns false only on I/O failure (cannot open the file). A malformed
    // JSON line is skipped and counted, never fatal; you cannot fix the
    // corpus from inside the indexer.
    bool build(const std::string& docsPath);

    // Access
    std::size_t size() const { return docs_.size(); }

    // Bounds-checked; throws std::out_of_range if docID is not valid. A bad
    // docID in search code is a bug (e.g. reading a stale index file), and
    // reading past the vector would be far worse than throwing.
    const DocRecord& get(std::uint32_t docID) const;


    // Persistence
    // Serialize to the binary format documented above. Returns false on
    // I/O failure or a write error mid-stream. Writes to a temp file first
    // and renames on success, so a crash cannot leave a half-written index
    // that a later load() would trust.
    bool save(const std::string& path) const;

    // Deserialize from the binary format. On any inconsistency (bad magic,
    // truncated record, trailing bytes) the in-memory index is cleared and
    // the method returns false, so a corrupted file can never produce a
    // partially-loaded index that disagrees with the inverted index.
    bool load(const std::string& path);

private:
    std::vector<DocRecord> docs_;   // docs_[docID] is the record for that docID

    // Binary I/O helpers (used only by save/load; in the .cpp)
    static void writeU32(std::string& out, std::uint32_t v);
    static void writeU16(std::string& out, std::uint16_t v);
    static void writeString(std::string& out, const std::string& s);
};

#endif