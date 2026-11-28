#ifndef INVERTED_INDEX_HPP
#define INVERTED_INDEX_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class ForwardIndex;
class Lexicon;

class InvertedIndex {

public:

    // Information about a word in one document.
    struct Posting {

        uint32_t docID;
        uint32_t tf;
        uint32_t positionOffset;

        uint32_t titleTF;
        uint32_t abstractTF;

        uint32_t bodyTF() const {
            return tf - titleTF - abstractTF;
        }
    };

    // All documents and positions belonging to a word.
    struct PostingList {

        std::vector<Posting> postings;
        std::vector<uint32_t> positions;

        std::size_t documentFrequency() const {
            return postings.size();
        }
    };

    bool build(
        const ForwardIndex& forward,
        const Lexicon& lexicon,
        const std::string& docsPath
    );

    bool save(const std::string& path) const;

    bool load(const std::string& path);

    const PostingList& get(uint32_t wordID) const {
        return terms_.at(wordID);
    }

    std::size_t size() const {
        return terms_.size();
    }

    uint32_t documentCount() const {
        return docCount_;
    }

private:

    // The wordID is also the index of its posting list.
    std::vector<PostingList> terms_;

    uint32_t docCount_ = 0;
};

#endif