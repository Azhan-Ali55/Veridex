#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <limits>
#include <cstring>
#include <cstdio>
#include <utility>

#include "inverted_index.hpp"
#include "forward_index.hpp"
#include "lexicon.hpp"
#include "json_util.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace std;

namespace {

    const char magic[] = "INVIDX02";

    const uint32_t maxCount =
        (numeric_limits<uint32_t>::max)();

    // Use the same byte order as the forward index.
    void writeNumber(ostream& out, uint32_t number) {

        unsigned char bytes[4];

        for (int i = 0; i < 4; ++i) {

            bytes[i] = static_cast<unsigned char>(
                number >> (8 * i)
                );
        }

        out.write(
            reinterpret_cast<const char*>(bytes), 4
        );
    }

    // Read a number back from the binary file.
    bool readNumber(istream& in, uint32_t& number) {

        unsigned char bytes[4];

        if (!in.read(reinterpret_cast<char*>(bytes), 4)) {
            return false;
        }

        number = 0;

        for (int i = 0; i < 4; ++i) {

            number |= static_cast<uint32_t>(bytes[i])
                << (8 * i);
        }

        return true;
    }

    bool mismatch(size_t docID) {

        cerr << "Documents, lexicon and forward index "
            << "disagree at docID " << docID << '\n';

        return false;
    }
}


// Build the inverted index using the forward index.
bool InvertedIndex::build(
    const ForwardIndex& forward,
    const Lexicon& lexicon,
    const string& docsPath
) {

    ifstream input(docsPath);

    if (!input ||
        forward.size() > maxCount ||
        lexicon.size() > maxCount) {

        return false;
    }

    // Build a fresh index so the old one stays safe
    // if something goes wrong.
    InvertedIndex next;

    next.docCount_ =
        static_cast<uint32_t>(forward.size());

    // Each word gets its own posting list.
    next.terms_.resize(lexicon.size());

    vector<uint32_t> positionCounts(lexicon.size(), 0);
    vector<uint32_t> postingCounts(lexicon.size(), 0);


    // Count how much space each posting list needs.
    // This avoids repeated memory allocations.
    for (size_t docID = 0;
        docID < forward.size();
        ++docID) {

        for (const auto& entry :
            forward.get(static_cast<uint32_t>(docID))) {

            if (entry.wordID >= next.terms_.size() ||
                entry.tf == 0 ||
                entry.tf >
                maxCount - positionCounts[entry.wordID]) {

                return mismatch(docID);
            }

            postingCounts[entry.wordID]++;

            positionCounts[entry.wordID] += entry.tf;
        }
    }


    // Reserve memory before creating the posting lists.
    for (size_t id = 0; id < next.terms_.size(); ++id) {

        next.terms_[id].postings.reserve(
            postingCounts[id]
        );

        next.terms_[id].positions.resize(
            positionCounts[id]
        );

        // We can reuse this counter as a position offset.
        positionCounts[id] = 0;
    }


    // Main inverted indexing logic.
    // Change docID -> words into wordID -> documents.
    for (size_t docID = 0;
        docID < forward.size();
        ++docID) {

        for (const auto& entry :
            forward.get(static_cast<uint32_t>(docID))) {

            PostingList& list =
                next.terms_[entry.wordID];

            // The forward index already counted
            // repeated words, so we can use its TF.
            list.postings.push_back({

                static_cast<uint32_t>(docID),
                entry.tf,
                positionCounts[entry.wordID],
                0,
                0
                });

            positionCounts[entry.wordID] += entry.tf;
        }
    }


    // The forward index does not store word positions.
    // Read the original documents to recover them.
    vector<size_t> currentPosting(lexicon.size(), 0);

    vector<uint32_t> written(lexicon.size(), 0);

    string line;

    size_t docID = 0;


    while (getline(input, line)) {

        if (line.empty()) {
            continue;
        }

        const auto doc =
            nlohmann::json::parse(line, nullptr, false);

        // Skip invalid documents just like the
        // forward index does.
        if (doc.is_discarded() || !doc.is_object()) {
            continue;
        }

        if (docID >= forward.size()) {
            return mismatch(docID);
        }


        // Get the three searchable fields.
        const string fields[] = {

            json_util::stringField(doc, "title"),

            json_util::stringField(doc, "abstract"),

            Lexicon::cleanBody(
                json_util::stringField(doc, "text")
            )
        };

        uint32_t position = 0;

        const uint32_t length =
            forward.docLength(
                static_cast<uint32_t>(docID)
            );


        // Process title, abstract and body in order.
        for (int field = 0; field < 3; ++field) {

            const auto tokens =
                Lexicon::tokenize(fields[field]);

            for (const string& word : tokens) {

                // Use the wordID from the existing lexicon.
                int id = lexicon.getId(word);

                if (id < 0 ||
                    static_cast<size_t>(id) >=
                    next.terms_.size() ||
                    position >= length) {

                    return mismatch(docID);
                }

                PostingList& list = next.terms_[id];

                if (currentPosting[id] >=
                    list.postings.size()) {

                    return mismatch(docID);
                }

                Posting& posting =
                    list.postings[currentPosting[id]];

                if (posting.docID != docID ||
                    written[id] >= posting.tf) {

                    return mismatch(docID);
                }


                // Store the location of this word.
                list.positions[
                    posting.positionOffset + written[id]
                ] = position++;

                written[id]++;


                // Keep title and abstract counts separate
                // so they can have more weight in ranking.
                if (field == 0) {
                    posting.titleTF++;
                }

                if (field == 1) {
                    posting.abstractTF++;
                }
            }
        }


        // Make sure the token count matches the
        // document length in the forward index.
        if (position != length) {
            return mismatch(docID);
        }


        // Check that every word occurred exactly
        // as many times as the forward index says.
        for (const auto& entry :
            forward.get(static_cast<uint32_t>(docID))) {

            if (written[entry.wordID] != entry.tf) {

                return mismatch(docID);
            }

            written[entry.wordID] = 0;

            currentPosting[entry.wordID]++;
        }

        docID++;
    }


    // Both indexes should contain the same documents.
    if (input.bad() ||
        !input.eof() ||
        docID != forward.size()) {

        return mismatch(docID);
    }

    *this = move(next);

    return true;
}


// Save the inverted index to a binary file.
bool InvertedIndex::save(const string& path) const {

    // Write to a temporary file first.
    const string temp = path + ".tmp";

    ofstream out(temp, ios::binary | ios::trunc);

    if (!out) {
        return false;
    }


    // Write the header and total counts.
    out.write(magic, 8);

    writeNumber(out, docCount_);

    writeNumber(
        out,
        static_cast<uint32_t>(terms_.size())
    );


    // Save each word's posting list.
    for (const PostingList& list : terms_) {

        writeNumber(
            out,
            static_cast<uint32_t>(list.postings.size())
        );

        writeNumber(
            out,
            static_cast<uint32_t>(list.positions.size())
        );


        // Save document IDs and word frequencies.
        for (const Posting& posting : list.postings) {

            writeNumber(out, posting.docID);

            writeNumber(out, posting.tf);

            writeNumber(out, posting.titleTF);

            writeNumber(out, posting.abstractTF);
        }


        // Save the positions belonging to this word.
        for (uint32_t position : list.positions) {

            writeNumber(out, position);
        }

        if (!out) {
            break;
        }
    }

    out.close();

    if (!out) {

        remove(temp.c_str());

        return false;
    }


    // Replace the old file after writing is complete.
#ifdef _WIN32

    return MoveFileExA(
        temp.c_str(),
        path.c_str(),
        MOVEFILE_REPLACE_EXISTING
    ) != 0;

#else

    return rename(
        temp.c_str(),
        path.c_str()
    ) == 0;

#endif
}


// Load a previously saved inverted index.
bool InvertedIndex::load(const string& path) {

    ifstream in(path, ios::binary | ios::ate);

    if (!in) {
        return false;
    }

    const streamoff fileSize = in.tellg();

    if (fileSize < 16) {
        return false;
    }

    uint64_t remaining =
        static_cast<uint64_t>(fileSize) - 16;

    in.seekg(0);

    char header[8];

    uint32_t termCount;

    InvertedIndex next;


    // Check whether this is a valid index file.
    if (!in.read(header, 8) ||
        memcmp(header, magic, 8) != 0 ||
        !readNumber(in, next.docCount_) ||
        !readNumber(in, termCount) ||
        static_cast<uint64_t>(termCount) * 8 > remaining) {

        return false;
    }

    next.terms_.resize(termCount);


    // Restore the posting list for every word.
    for (PostingList& list : next.terms_) {

        uint32_t df;
        uint32_t count;

        if (remaining < 8 ||
            !readNumber(in, df) ||
            !readNumber(in, count)) {

            return false;
        }

        remaining -= 8;

        uint64_t bytes =
            static_cast<uint64_t>(df) * 16 +
            static_cast<uint64_t>(count) * 4;


        // Check the sizes before allocating memory.
        if (df > next.docCount_ ||
            df > count ||
            bytes > remaining) {

            return false;
        }

        remaining -= bytes;

        list.postings.reserve(df);

        uint64_t offset = 0;


        // Read each document belonging to this word.
        for (uint32_t i = 0; i < df; ++i) {

            Posting p{};

            if (!readNumber(in, p.docID) ||
                !readNumber(in, p.tf) ||
                !readNumber(in, p.titleTF) ||
                !readNumber(in, p.abstractTF) ||
                p.tf == 0 ||
                p.docID >= next.docCount_ ||
                static_cast<uint64_t>(p.titleTF) +
                p.abstractTF > p.tf ||
                offset + p.tf > count ||
                (i > 0 &&
                    p.docID <= list.postings.back().docID)) {

                return false;
            }


            // The offset can be calculated from
            // the frequencies of earlier postings.
            p.positionOffset =
                static_cast<uint32_t>(offset);

            offset += p.tf;

            list.postings.push_back(p);
        }

        if (offset != count) {
            return false;
        }


        // Restore all saved positions.
        list.positions.resize(count);

        for (uint32_t& position : list.positions) {

            if (!readNumber(in, position)) {
                return false;
            }
        }


        // Positions within a document should be sorted.
        for (const Posting& p : list.postings) {

            for (uint32_t i = 1; i < p.tf; ++i) {

                if (list.positions[
                    p.positionOffset + i - 1
                ] >=
                    list.positions[
                        p.positionOffset + i
                    ]) {

                    return false;
                }
            }
        }
    }


    // Reject incomplete files or unexpected extra data.
    if (remaining != 0 ||
        in.peek() != char_traits<char>::eof() ||
        in.bad()) {

        return false;
    }

    *this = move(next);

    return true;
}