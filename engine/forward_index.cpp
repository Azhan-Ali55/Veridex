#include "forward_index.hpp"
#include "lexicon.hpp"
#include "json.hpp"

#include <cstdio>       // std::rename, std::remove
#include <cstring>      // std::memcpy
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

using json = nlohmann::json;

// The on-disk magic. Bump this if the record layout ever changes, so an old
// file fails the equality check in load() instead of being misinterpreted.
constexpr char kMagic[8] = {'F', 'W', 'D', 'I', 'D', 'X', '0', '1'};

// Pull a string field out of a JSON object safely.
// Returns "" when the key is missing, the value is JSON null, or the value
// is not a JSON string. The Federal Register corpus sets `abstract` to null
// on many records, so this must not throw.
std::string stringField(const json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        return std::string();
    }
    return it->get<std::string>();
}

// Pull an array-of-strings field (e.g. agency_names). Returns an empty vector
// when the field is missing or not an array. Non-string entries are skipped,
// which is more robust than rejecting the whole record for one odd value.
std::vector<std::string> stringArrayField(const json& obj, const char* key) {
    std::vector<std::string> out;
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_array()) {
        return out;
    }
    out.reserve(it->size());
    for (const auto& item : *it) {
        if (item.is_string()) {
            out.push_back(item.get<std::string>());
        }
    }
    return out;
}

} 

// BINARY I/O HELPERS
// Everything is little-endian. All target platforms for this project are
// x86-64 / ARM64 running a mainstream OS, where little-endian is the default;
// we do not do byte-swapping because doing so would add code that is never
// exercised. If portability ever matters, this is the one place to fix.

void ForwardIndex::writeU32(std::string& out, std::uint32_t v) {
    char buf[4];
    std::memcpy(buf, &v, 4);
    out.append(buf, 4);
}

void ForwardIndex::writeU16(std::string& out, std::uint16_t v) {
    char buf[2];
    std::memcpy(buf, &v, 2);
    out.append(buf, 2);
}

void ForwardIndex::writeString(std::string& out, const std::string& s) {
    // uint32 length prefix so the reader knows exactly how many bytes to
    // consume, even if the string contains NULs (titles never do, but
    // abstracts occasionally carry strange control bytes).
    writeU32(out, static_cast<std::uint32_t>(s.size()));
    out.append(s);
}

// BUILD
bool ForwardIndex::build(const std::string& docsPath) {
    std::ifstream input(docsPath);
    if (!input.is_open()) {
        std::cerr << "ForwardIndex: cannot open " << docsPath << '\n';
        return false;
    }

    // Idempotent: a second build() must not accumulate stale records from a
    // previous corpus, because that would silently shift docIDs.
    docs_.clear();

    std::string line;
    std::size_t badLines = 0;

    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        // No-throw parse. Bad lines are skipped and counted; they do NOT
        // consume a docID, because the inverted indexer will apply the same
        // rule and must arrive at the same docID -> document mapping.
        const json doc = json::parse(line, nullptr, false);
        if (doc.is_discarded() || !doc.is_object()) {
            ++badLines;
            continue;
        }

        DocRecord rec;
        rec.documentNumber = stringField(doc, "document_number");
        rec.title          = stringField(doc, "title");
        rec.abstract       = stringField(doc, "abstract");
        rec.publicationDate = stringField(doc, "publication_date");
        rec.docType        = stringField(doc, "type");
        rec.agencies       = stringArrayField(doc, "agency_names");

        // bodyLength must count exactly the tokens the inverted indexer will
        // emit for the body, so it uses the same cleanBody + tokenize. That
        // is the whole reason those functions live in Lexicon, not here.
        const std::string cleanedBody = Lexicon::cleanBody(stringField(doc, "text"));
        rec.bodyLength = static_cast<std::uint32_t>(
            Lexicon::tokenize(cleanedBody).size());

        docs_.push_back(std::move(rec));
    }

    std::cout << "ForwardIndex: " << docs_.size() << " docs indexed ("
              << badLines << " bad lines skipped)\n";
    return true;
}

// ACCESS
const ForwardIndex::DocRecord& ForwardIndex::get(std::uint32_t docID) const {
    if (docID >= docs_.size()) {
        throw std::out_of_range("ForwardIndex::get: invalid docID");
    }
    return docs_[docID];
}

// SAVE
bool ForwardIndex::save(const std::string& path) const {
    // Buffer the whole file in memory first. For 60k docs with modest
    // metadata this is a few tens of MB, which is fine and makes the write
    // a single fwrite (one syscall, no interleaving risk on a crash).
    std::string buf;
    buf.reserve(8 + 4 + docs_.size() * 512); // rough size guess

    buf.append(kMagic, 8);

    // docCount in uint32; the project's scale is far below 4 billion docs.
    writeU32(buf, static_cast<std::uint32_t>(docs_.size()));

    for (const DocRecord& rec : docs_) {
        writeString(buf, rec.documentNumber);
        writeString(buf, rec.title);
        writeString(buf, rec.abstract);
        writeString(buf, rec.publicationDate);
        writeString(buf, rec.docType);

        // agencies: 2-byte count, then the strings. 2 bytes is plenty for
        // the FR corpus, where a document has at most a handful of agencies.
        writeU16(buf, static_cast<std::uint16_t>(rec.agencies.size()));
        for (const std::string& a : rec.agencies) {
            writeString(buf, a);
        }

        writeU32(buf, rec.bodyLength);
    }

    // Atomic write: write to a sibling temp file, then rename. If we crash
    // mid-write, the real file is untouched, so a later load() sees either
    // the old complete file or the new complete file, never a truncation.
    const std::string tmpPath = path + ".tmp";

    {
        std::ofstream out(tmpPath, std::ios::binary);
        if (!out.is_open()) {
            return false;
        }
        out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
        if (!out.good()) {
            out.close();
            std::remove(tmpPath.c_str());
            return false;
        }
    } // close before rename; required on Windows, harmless on POSIX

    // std::rename overwrites the destination on POSIX and on Windows when
    // the destination is not open. If the destination exists and is locked
    // by another process, this fails; that is the caller's problem.
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        std::remove(tmpPath.c_str());
        return false;
    }
    return true;
}

// LOAD
bool ForwardIndex::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    // Read the entire file into memory. Startup cost is one fread; from
    // then on, decoding is pure pointer arithmetic with no syscalls. This
    // matters for the search tool, which may be launched thousands of times
    // in a benchmark loop.
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    if (data.size() < 12) {           // magic(8) + docCount(4) minimum
        return false;
    }

    // Fail loudly if the file was written by a different format version.
    if (std::memcmp(data.data(), kMagic, 8) != 0) {
        return false;
    }

    std::size_t cursor = 8;

    // Helper lambdas read one field and advance the cursor. They return
    // false on any truncation so the outer loop can bail out atomically.
    auto readU32 = [&](std::uint32_t& v) -> bool {
        if (cursor + 4 > data.size()) return false;
        std::memcpy(&v, data.data() + cursor, 4);
        cursor += 4;
        return true;
    };
    auto readU16 = [&](std::uint16_t& v) -> bool {
        if (cursor + 2 > data.size()) return false;
        std::memcpy(&v, data.data() + cursor, 2);
        cursor += 2;
        return true;
    };
    auto readString = [&](std::string& s) -> bool {
        std::uint32_t len = 0;
        if (!readU32(len)) return false;
        if (cursor + len > data.size()) return false;
        s.assign(data.data() + cursor, len);
        cursor += len;
        return true;
    };

    std::uint32_t docCount = 0;
    if (!readU32(docCount)) {
        return false;
    }

    // Build into a local vector, then swap. That way a corrupt file leaves
    // the caller's existing index untouched rather than half-destroyed.
    std::vector<DocRecord> loaded;
    loaded.reserve(docCount);

    for (std::uint32_t i = 0; i < docCount; ++i) {
        DocRecord rec;
        if (!readString(rec.documentNumber) ||
            !readString(rec.title) ||
            !readString(rec.abstract) ||
            !readString(rec.publicationDate) ||
            !readString(rec.docType)) {
            return false;
        }

        std::uint16_t agencyCount = 0;
        if (!readU16(agencyCount)) {
            return false;
        }
        rec.agencies.reserve(agencyCount);
        for (std::uint16_t a = 0; a < agencyCount; ++a) {
            std::string name;
            if (!readString(name)) {
                return false;
            }
            rec.agencies.push_back(std::move(name));
        }

        if (!readU32(rec.bodyLength)) {
            return false;
        }

        loaded.push_back(std::move(rec));
    }

    // Trailing bytes mean the file was written by a newer format or got
    // concatenated to another file. Either way, do not trust it.
    if (cursor != data.size()) {
        return false;
    }

    docs_.swap(loaded);
    return true;
}