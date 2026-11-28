// PURPOSE
//   The forward index records, for every document, which words it contains
//   and how often. It is the intermediate structure from which the inverted
//   index (wordID -> list of docIDs) is produced, and it also answers
//   per-document questions at query time (document length, the frequency of
//   a given term inside a given document).
//
// THE docID CONTRACT
//   docIDs are assigned 0, 1, 2, ... to the valid JSON objects of docs.jsonl
//   in file order. Blank lines and lines that are not valid JSON objects are
//   skipped and do NOT consume a docID. Every other component that numbers
//   documents (the document-metadata store, the inverted-index builder) MUST
//   apply exactly the same rule, otherwise docIDs will silently point at the
//   wrong documents.
//
// WHAT IS INDEXED
//   Exactly what the lexicon indexes: title, abstract and Lexicon::cleanBody
//   (text), each run through Lexicon::tokenize. Because the lexicon and the
//   forward index share those functions, every token is guaranteed to be
//   found in the lexicon -- provided the lexicon was built from the same
//   docs.jsonl. A token that is NOT found indicates a stale lexicon; such
//   tokens are dropped and counted (see BuildStats::lexiconMisses), and the
//   driver refuses to save the result.
//
// IN-MEMORY LAYOUT
//   All postings live in ONE contiguous array of Entry records, sorted by
//   (docID, wordID). A second array of offsets marks where each document's
//   slice begins:
//
//       offsets_  : [ 0 | 5 | 9 | 9 | 14 | ... ]      (docCount + 1 values)
//       entries_  : [ d0:w3 w7 w9 w12 w40 | d1:... ]
//
//   Compared with a vector<vector<...>> this needs no per-document
//   allocation, loads much faster and is friendlier to the CPU cache.
//
// ON-DISK FORMAT (forward_index.bin, all integers little-endian)
//
//   offset  size   field
//   ------  -----  --------------------------------------------------------
//   0       8      magic "FWDIDX03"
//   8       4      docCount     (uint32)
//   12      8      entryCount   (uint64)  total (wordID, tf) pairs in file
//   20      8*D    doc table: for each document { termCount u32, length u32 }
//                  termCount = number of distinct words,
//                  length    = number of tokens (sum of its tf values)
//   20+8*D  8*E    entries:   for each document, in docID order, termCount
//                  records { wordID u32, tf u32 } sorted by wordID ascending
//
//   The total file size is fully determined by docCount and entryCount, so
//   load() checks it before allocating anything. A truncated, extended or
//   hand-edited file is therefore rejected rather than trusted.
//
// THREAD SAFETY
//   A loaded ForwardIndex is immutable; concurrent const access is safe.

#ifndef FORWARD_INDEX_HPP
#define FORWARD_INDEX_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Lexicon;

class ForwardIndex {
public:
    // One (word, frequency) pair belonging to a document.
    // Deliberately a plain aggregate: no default member initialisers, so it
    // stays trivially copyable and can be brace-initialised in C++11.
    struct Entry {
        std::uint32_t wordID;
        std::uint32_t tf;   // occurrences of wordID in the document, >= 1
    };

    // Read-only view of one document's entries, sorted by wordID ascending.
    // It points into the index, so it is valid only while the ForwardIndex
    // it came from is alive and unmodified (build()/load() invalidate it).
    class TermList {
    public:
        TermList(const Entry* first, std::size_t count)
            : first_(first), count_(count) {}

        const Entry* begin() const { return first_; }
        const Entry* end() const { return first_ + count_; }
        std::size_t size() const { return count_; }
        bool empty() const { return count_ == 0; }
        const Entry& operator[](std::size_t i) const { return first_[i]; }

    private:
        const Entry* first_;
        std::size_t count_;
    };

    // Diagnostics from the most recent build().
    struct BuildStats {
        std::size_t documents = 0;       // valid documents indexed
        std::size_t badLines = 0;        // non-blank lines that were not valid JSON objects
        std::size_t lexiconMisses = 0;   // tokens missing from the lexicon (should be 0)
    };

    ForwardIndex();

    // construction

    // Build the index from a JSONL corpus, resolving words through `lexicon`
    // (which must already be built or loaded from the same corpus).
    //
    // Returns false if the file cannot be opened or a document is too large
    // to represent; in that case the index keeps its previous contents.
    // Malformed JSON lines are never fatal: they are skipped and counted.
    bool build(const std::string& docsPath, const Lexicon& lexicon);

    // Statistics of the last build(). Zero-initialised before the first build.
    const BuildStats& stats() const { return stats_; }

    // access

    // Number of documents.
    std::size_t size() const { return docLengths_.size(); }

    // Total number of (docID, wordID) pairs stored.
    std::size_t entryCount() const { return entries_.size(); }

    // The terms of one document, sorted by wordID.
    // Throws std::out_of_range if docID >= size(): an invalid docID means
    // the caller is holding a stale or mismatched index, which should fail
    // loudly rather than read out of bounds.
    TermList get(std::uint32_t docID) const;

    // Document length in tokens (title + abstract + body), i.e. the sum of
    // the document's tf values. This is the |d| used by length-normalised
    // ranking functions such as BM25. Throws std::out_of_range like get().
    std::uint32_t docLength(std::uint32_t docID) const;

    // Frequency of `wordID` in document `docID`; 0 if it does not occur.
    // O(log k) where k is the number of distinct words in the document.
    // Throws std::out_of_range like get().
    std::uint32_t termFrequency(std::uint32_t docID, std::uint32_t wordID) const;

    // persistence

    // Write the index to `path` in the format above.
    // The data is first written to "<path>.tmp" and then moved into place, so
    // a crash during the write never leaves a half-written index under the
    // real name. (The final move is atomic on POSIX; on Windows it is a
    // remove-then-rename, which leaves a very small window in which only the
    // .tmp file exists. That file is kept on failure so nothing is lost.)
    // Returns false on any I/O error.
    bool save(const std::string& path) const;

    // Replace the contents with the index stored at `path`.
    // The file is validated completely -- magic, exact size, offsets,
    // ascending wordIDs, non-zero tf, per-document length -- BEFORE this
    // object is modified. On any problem the method returns false and the
    // existing contents are left untouched.
    bool load(const std::string& path);

private:
    std::vector<Entry>         entries_;     // all postings, grouped by document
    std::vector<std::uint64_t> offsets_;     // size() + 1 slice boundaries into entries_
    std::vector<std::uint32_t> docLengths_;  // tokens per document
    BuildStats                 stats_;
};

#endif // FORWARD_INDEX_HPP