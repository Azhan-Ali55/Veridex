// See forward_index.hpp for the contract, the in-memory layout and the
// on-disk format. This file contains no main(); engine/main_forward.cpp is the
// command-line driver.
//
// Build (from the repository root), C++11:
//   g++ -std=c++11 -O2 engine/main_forward.cpp engine/forward_index.cpp engine/lexicon.cpp -o engine/forward_index.exe

#include "forward_index.hpp"
#include "json_util.hpp"
#include "lexicon.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace {
// File-format constants
// Format identifier. Bump the trailing digits whenever the layout changes so
// an index written by an older build is rejected instead of misread.
constexpr char kMagic[8] = {'F', 'W', 'D', 'I', 'D', 'X', '0', '3'};

constexpr std::size_t kMagicSize     = 8;
constexpr std::size_t kHeaderSize    = kMagicSize + 4 + 8;  // magic + docCount + entryCount
constexpr std::size_t kDocRecordSize = 8;                   // termCount u32 + length u32
constexpr std::size_t kEntrySize     = 8;                   // wordID u32 + tf u32

// Entries are streamed to / from disk in blocks of this many records
// (512 KiB) so that neither save() nor load() needs a second full-size copy
// of the data in memory.
constexpr std::size_t kBlockEntries = std::size_t(1) << 16;

// Print a progress line every this many documents while building.
constexpr std::size_t kProgressInterval = 5000;

// ---------------------------------------------------------------------------
// Explicit little-endian encoding
//
// Bytes are composed with shifts instead of memcpy'ing the native integer, so
// the file format is identical on every platform and the code is correct on
// big-endian machines too.
// ---------------------------------------------------------------------------

void putU32(unsigned char* p, std::uint32_t v) {
    p[0] = static_cast<unsigned char>(v & 0xFFu);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFFu);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xFFu);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xFFu);
}

void putU64(unsigned char* p, std::uint64_t v) {
    putU32(p, static_cast<std::uint32_t>(v & 0xFFFFFFFFull));
    putU32(p + 4, static_cast<std::uint32_t>(v >> 32));
}

std::uint32_t getU32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t getU64(const unsigned char* p) {
    return static_cast<std::uint64_t>(getU32(p))
         | (static_cast<std::uint64_t>(getU32(p + 4)) << 32);
}

// Write `n` bytes; true if the stream is still healthy afterwards.
bool writeBytes(std::ofstream& out, const unsigned char* data, std::size_t n) {
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(n));
    return out.good();
}

// Read exactly `n` bytes; true only if all of them were available.
bool readBytes(std::ifstream& in, unsigned char* data, std::size_t n) {
    in.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(in.gcount()) == n;
}

// Move `tmp` over `dest`.
//
// POSIX rename() replaces an existing destination atomically. The Windows C
// runtime refuses to when the destination exists, so on failure we remove the
// destination and try once more. If that also fails the .tmp file is left in
// place on purpose: it holds the only complete copy of the new data.
bool replaceFile(const std::string& tmp, const std::string& dest) {
    if (std::rename(tmp.c_str(), dest.c_str()) == 0) {
        return true;
    }
    std::remove(dest.c_str());
    return std::rename(tmp.c_str(), dest.c_str()) == 0;
}

bool entryWordLess(const ForwardIndex::Entry& a, const ForwardIndex::Entry& b) {
    return a.wordID < b.wordID;
}

bool entryWordLessThanValue(const ForwardIndex::Entry& e, std::uint32_t wordID) {
    return e.wordID < wordID;
}

} // namespace

// CONSTRUCTION
// offsets_ always holds docCount + 1 boundaries; an empty index has the single
// boundary 0, which keeps get()/load() free of "is it empty?" special cases.
ForwardIndex::ForwardIndex() : offsets_(1, 0) {}

// BUILD
bool ForwardIndex::build(const std::string& docsPath, const Lexicon& lexicon) {
    std::ifstream input(docsPath);
    if (!input.is_open()) {
        std::cerr << "ForwardIndex: cannot open " << docsPath << '\n';
        return false;
    }

    // Build into locals and publish at the end, so a failure part-way through
    // cannot leave this object half-built.
    std::vector<Entry>         entries;
    std::vector<std::uint64_t> offsets(1, 0);
    std::vector<std::uint32_t> docLengths;
    BuildStats                 stats;

    // Scratch buffers reused for every document. clear() keeps the allocated
    // capacity, so after the first few documents the loop stops allocating.
    std::unordered_map<std::uint32_t, std::uint32_t> termCounts;
    std::vector<Entry> docEntries;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        // The `false` argument disables exceptions: a malformed line yields a
        // "discarded" value instead of throwing.
        const nlohmann::json doc = nlohmann::json::parse(line, nullptr, false);
        if (doc.is_discarded() || !doc.is_object()) {
            // Skipped lines must NOT consume a docID (see the docID contract).
            ++stats.badLines;
            continue;
        }

        // ---- count term frequencies for this document ----------------------
        termCounts.clear();
        std::uint64_t length = 0;

        // Tokenize one field and accumulate its words. Tokens unknown to the
        // lexicon are dropped and counted: that only happens when the lexicon
        // was built from a different corpus.
        auto countField = [&](const std::string& text) {
            const std::vector<std::string> tokens = Lexicon::tokenize(text);
            for (std::size_t i = 0; i < tokens.size(); ++i) {
                const int id = lexicon.getId(tokens[i]);
                if (id < 0) {
                    ++stats.lexiconMisses;
                    continue;
                }
                ++termCounts[static_cast<std::uint32_t>(id)];
                ++length;
            }
        };

        // The same three fields, in the same way, as Lexicon::build().
        countField(json_util::stringField(doc, "title"));
        countField(json_util::stringField(doc, "abstract"));
        countField(Lexicon::cleanBody(json_util::stringField(doc, "text")));

        // The file format stores lengths as uint32; refuse rather than wrap.
        if (length > std::numeric_limits<std::uint32_t>::max() ||
            docLengths.size() >= std::numeric_limits<std::uint32_t>::max()) {
            std::cerr << "ForwardIndex: document " << docLengths.size()
                      << " is too large to represent\n";
            return false;
        }

        // ---- emit the document's entries sorted by wordID ------------------
        // The inverted-index builder relies on this order to merge-scan.
        docEntries.clear();
        docEntries.reserve(termCounts.size());
        for (std::unordered_map<std::uint32_t, std::uint32_t>::const_iterator it =
                 termCounts.begin(); it != termCounts.end(); ++it) {
            Entry e;
            e.wordID = it->first;
            e.tf = it->second;
            docEntries.push_back(e);
        }
        std::sort(docEntries.begin(), docEntries.end(), entryWordLess);

        entries.insert(entries.end(), docEntries.begin(), docEntries.end());
        offsets.push_back(static_cast<std::uint64_t>(entries.size()));
        docLengths.push_back(static_cast<std::uint32_t>(length));

        if (docLengths.size() % kProgressInterval == 0) {
            std::cout << "  " << docLengths.size() << " docs, "
                      << entries.size() << " entries, "
                      << stats.lexiconMisses << " lexicon misses\n";
        }
    }

    stats.documents = docLengths.size();

    entries_.swap(entries);
    offsets_.swap(offsets);
    docLengths_.swap(docLengths);
    stats_ = stats;

    std::cout << "ForwardIndex: " << stats_.documents << " docs, "
              << entries_.size() << " entries (" << stats_.badLines
              << " bad lines skipped, " << stats_.lexiconMisses
              << " lexicon misses)\n";
    return true;
}

// ACCESS
ForwardIndex::TermList ForwardIndex::get(std::uint32_t docID) const {
    if (docID >= docLengths_.size()) {
        throw std::out_of_range("ForwardIndex::get: invalid docID");
    }
    const std::size_t begin = static_cast<std::size_t>(offsets_[docID]);
    const std::size_t end   = static_cast<std::size_t>(offsets_[docID + 1]);
    return TermList(entries_.data() + begin, end - begin);
}

std::uint32_t ForwardIndex::docLength(std::uint32_t docID) const {
    if (docID >= docLengths_.size()) {
        throw std::out_of_range("ForwardIndex::docLength: invalid docID");
    }
    return docLengths_[docID];
}

std::uint32_t ForwardIndex::termFrequency(std::uint32_t docID,
                                          std::uint32_t wordID) const {
    const TermList terms = get(docID);

    // The entries are sorted by wordID, so a binary search suffices.
    const Entry* it = std::lower_bound(terms.begin(), terms.end(), wordID,
                                       entryWordLessThanValue);
    return (it != terms.end() && it->wordID == wordID) ? it->tf : 0;
}

// SAVE
bool ForwardIndex::save(const std::string& path) const {
    const std::string tmp = path + ".tmp";

    // Remove the temp file after a failed write; the real file is untouched.
    // (The block below closes the stream first, which Windows requires.)
    bool ok = false;
    {
        std::ofstream out(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }

        // header
        unsigned char header[kHeaderSize];
        std::memcpy(header, kMagic, kMagicSize);
        putU32(header + 8, static_cast<std::uint32_t>(docLengths_.size()));
        putU64(header + 12, static_cast<std::uint64_t>(entries_.size()));
        ok = writeBytes(out, header, kHeaderSize);

        // doc table: one { termCount, length } record per document 
        if (ok && !docLengths_.empty()) {
            std::vector<unsigned char> table(docLengths_.size() * kDocRecordSize);
            for (std::size_t d = 0; d < docLengths_.size(); ++d) {
                const std::uint64_t terms = offsets_[d + 1] - offsets_[d];
                putU32(&table[d * kDocRecordSize],
                       static_cast<std::uint32_t>(terms));
                putU32(&table[d * kDocRecordSize + 4], docLengths_[d]);
            }
            ok = writeBytes(out, &table[0], table.size());
        }

        // entries, streamed in fixed-size blocks 
        std::vector<unsigned char> block;
        block.reserve(kBlockEntries * kEntrySize);
        for (std::size_t i = 0; ok && i < entries_.size(); ) {
            const std::size_t n = std::min(kBlockEntries, entries_.size() - i);
            block.resize(n * kEntrySize);
            for (std::size_t k = 0; k < n; ++k) {
                putU32(&block[k * kEntrySize],     entries_[i + k].wordID);
                putU32(&block[k * kEntrySize + 4], entries_[i + k].tf);
            }
            ok = writeBytes(out, &block[0], block.size());
            i += n;
        }

        // flush() surfaces errors (e.g. disk full) that buffered writes hide.
        out.flush();
        ok = ok && out.good();
    }

    if (!ok) {
        std::remove(tmp.c_str());
        return false;
    }
    // On failure replaceFile() deliberately keeps the .tmp file (see above).
    return replaceFile(tmp, path);
}

// LOAD
bool ForwardIndex::load(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    // file size
    in.seekg(0, std::ios::end);
    const std::streamoff fileSizeSigned = in.tellg();
    if (fileSizeSigned < 0 ||
        static_cast<std::uint64_t>(fileSizeSigned) < kHeaderSize) {
        return false;
    }
    const std::uint64_t fileSize = static_cast<std::uint64_t>(fileSizeSigned);
    in.seekg(0, std::ios::beg);

    // header 
    unsigned char header[kHeaderSize];
    if (!readBytes(in, header, kHeaderSize)) {
        return false;
    }
    if (std::memcmp(header, kMagic, kMagicSize) != 0) {
        return false;   // wrong file, or written by a different format version
    }
    const std::uint64_t docCount   = getU32(header + 8);
    const std::uint64_t entryCount = getU64(header + 12);

    // exact-size check, BEFORE any allocation 
    // The header fully determines the file size. Verifying it first means a
    // corrupt count can never trigger a huge allocation (std::bad_alloc), and
    // a truncated or over-long file is rejected up front. The divisions guard
    // against arithmetic overflow on adversarial counts.
    const std::uint64_t tableBytes = docCount * kDocRecordSize;   // <= 32 GiB, no overflow
    if (entryCount > (std::numeric_limits<std::uint64_t>::max() - kHeaderSize - tableBytes)
                         / kEntrySize) {
        return false;
    }
    const std::uint64_t expectedSize = kHeaderSize + tableBytes + entryCount * kEntrySize;
    if (fileSize != expectedSize) {
        return false;
    }

    // Everything is decoded into locals and swapped in only at the very end.
    std::vector<std::uint64_t> offsets;
    std::vector<std::uint32_t> docLengths;
    std::vector<Entry>         entries;

    // doc table
    offsets.reserve(static_cast<std::size_t>(docCount) + 1);
    docLengths.reserve(static_cast<std::size_t>(docCount));
    offsets.push_back(0);
    if (docCount > 0) {
        std::vector<unsigned char> table(static_cast<std::size_t>(tableBytes));
        if (!readBytes(in, &table[0], table.size())) {
            return false;
        }
        std::uint64_t running = 0;
        for (std::uint64_t d = 0; d < docCount; ++d) {
            const unsigned char* rec = &table[static_cast<std::size_t>(d * kDocRecordSize)];
            running += getU32(rec);
            if (running > entryCount) {
                return false;   // per-document counts overrun the entry area
            }
            offsets.push_back(running);
            docLengths.push_back(getU32(rec + 4));
        }
    }
    if (offsets.back() != entryCount) {
        return false;           // counts do not add up to the stored total
    }

    // entries, streamed in blocks
    entries.reserve(static_cast<std::size_t>(entryCount));
    std::vector<unsigned char> block;
    for (std::uint64_t done = 0; done < entryCount; ) {
        const std::size_t n = static_cast<std::size_t>(
            std::min<std::uint64_t>(kBlockEntries, entryCount - done));
        block.resize(n * kEntrySize);
        if (!readBytes(in, &block[0], block.size())) {
            return false;
        }
        for (std::size_t k = 0; k < n; ++k) {
            Entry e;
            e.wordID = getU32(&block[k * kEntrySize]);
            e.tf     = getU32(&block[k * kEntrySize + 4]);
            entries.push_back(e);
        }
        done += n;
    }

    // semantic validation
    // Per document: wordIDs strictly ascending (so unique), every tf >= 1,
    // and the stored length equals the sum of the tf values. The builder
    // guarantees all three, so a violation means the file was damaged.
    for (std::size_t d = 0; d < docLengths.size(); ++d) {
        const std::size_t begin = static_cast<std::size_t>(offsets[d]);
        const std::size_t end   = static_cast<std::size_t>(offsets[d + 1]);
        std::uint64_t sum = 0;
        for (std::size_t k = begin; k < end; ++k) {
            if (entries[k].tf == 0) {
                return false;
            }
            if (k > begin && entries[k].wordID <= entries[k - 1].wordID) {
                return false;
            }
            sum += entries[k].tf;
        }
        if (sum != docLengths[d]) {
            return false;
        }
    }

    // publish
    entries_.swap(entries);
    offsets_.swap(offsets);
    docLengths_.swap(docLengths);
    stats_ = BuildStats();   // statistics describe a build, not a loaded file
    return true;
}