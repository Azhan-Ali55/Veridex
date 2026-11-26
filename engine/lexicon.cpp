// lexicon.cpp -- builds the lexicon (word -> wordID) from docs.jsonl.
//
// Build (from the Veridex folder):
//   g++ -std=c++11 -O2 engine/lexicon.cpp -o engine/lexicon.exe
// Run:
//   ./engine/lexicon.exe sample/sample_500.jsonl lexicon.txt
//
// Output file format (see DESIGN.md):  wordID<TAB>word   (one word per line)

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include "json.hpp"

class Lexicon {
public:
    // ---------- tokenizer: THE shared rules from DESIGN.md ----------
    // Lowercase; a word is a run of a-z / 0-9; everything else separates words;
    // keep only tokens of length 2..30; no stopword removal.
    // Static so teammates can call Lexicon::tokenize(...) for documents AND for queries.
    static std::vector<std::string> tokenize(const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        for (size_t i = 0; i <= s.size(); i++) {
            char c = (i < s.size()) ? s[i] : ' ';           // sentinel space flushes the last word
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';    // manual lowercase (ASCII only)
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                cur += c;
            } else {                                        // separator (also all non-ASCII bytes)
                if (cur.size() >= 2 && cur.size() <= 30) out.push_back(cur);
                cur.clear();
            }
        }
        return out;
    }

    // ---------- body cleanup ----------
    // The downloaded text starts with a GPO header and has "[[Page 43]]" markers inside.
    // Skip everything before the first "[[Page" marker, then drop every "[[...]]" marker.
    static std::string cleanBody(const std::string& t) {
        size_t start = t.find("[[Page");
        std::string s = (start == std::string::npos) ? t : t.substr(start);  // no marker: keep all
        std::string out;
        size_t i = 0;
        while (i < s.size()) {
            if (s.compare(i, 2, "[[") == 0) {               // marker begins
                size_t end = s.find("]]", i);
                if (end == std::string::npos) break;        // malformed: stop
                i = end + 2;                                // jump past the marker
            } else {
                out += s[i++];
            }
        }
        return out;
    }

    // ---------- add one word; returns its wordID ----------
    int addWord(const std::string& w) {
        auto it = ids_.find(w);
        if (it != ids_.end()) return it->second;            // already known
        int id = (int)words_.size();                        // next free ID = 0,1,2,... by first appearance
        ids_[w] = id;
        words_.push_back(w);
        return id;
    }

    // ---------- lookup: -1 if the word is not in the lexicon ----------
    int getId(const std::string& w) const {
        auto it = ids_.find(w);
        return it == ids_.end() ? -1 : it->second;
    }

    size_t size() const { return words_.size(); }
    const std::string& getWord(int id) const { return words_[id]; }

    // ---------- build from docs.jsonl (one JSON document per line) ----------
    // Reads line by line, so the whole file is never held in RAM.
    bool build(const std::string& docsPath) {
        std::ifstream in(docsPath);
        if (!in) { std::cerr << "Cannot open " << docsPath << "\n"; return false; }
        std::string line;
        size_t docs = 0, bad = 0;
        auto t0 = std::chrono::steady_clock::now();
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            nlohmann::json d = nlohmann::json::parse(line, nullptr, false);  // false = don't throw
            if (d.is_discarded() || !d.is_object()) { bad++; continue; }     // skip broken lines
            // title, abstract and text are indexed; any of them may be null/missing
            addText(strField(d, "title"));
            addText(strField(d, "abstract"));
            addText(cleanBody(strField(d, "text")));
            docs++;
            if (docs % 5000 == 0) std::cout << "  " << docs << " docs, " << words_.size() << " words\n";
        }
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "Done: " << docs << " docs (" << bad << " bad lines), "
                  << words_.size() << " unique words, " << secs << " s\n";
        return true;
    }

    // ---------- save as lexicon.txt: wordID<TAB>word ----------
    bool save(const std::string& path) const {
        std::ofstream out(path);
        if (!out) return false;
        for (size_t i = 0; i < words_.size(); i++) out << i << '\t' << words_[i] << '\n';
        return true;
    }

    // ---------- load an existing lexicon.txt (used later by indexer + search) ----------
    bool load(const std::string& path) {
        std::ifstream in(path);
        if (!in) return false;
        ids_.clear(); words_.clear();
        std::string line;
        while (std::getline(in, line)) {
            size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            addWord(line.substr(tab + 1));                  // IDs are the line order, so they match
        }
        return true;
    }

private:
    std::unordered_map<std::string, int> ids_;              // word -> wordID (fast lookup)
    std::vector<std::string> words_;                        // wordID -> word (index = ID)

    void addText(const std::string& s) {
        for (const std::string& w : tokenize(s)) addWord(w);
    }

    // Safe string getter: returns "" when the field is missing or null (abstract can be null).
    static std::string strField(const nlohmann::json& d, const char* key) {
        auto it = d.find(key);
        if (it == d.end() || !it->is_string()) return "";
        return it->get<std::string>();
    }
};

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <docs.jsonl> <lexicon.txt>\n";
        return 1;
    }
    Lexicon lex;
    if (!lex.build(argv[1])) return 1;
    if (!lex.save(argv[2])) { std::cerr << "Cannot write " << argv[2] << "\n"; return 1; }
    std::cout << "Saved " << lex.size() << " words to " << argv[2] << "\n";
    return 0;
}