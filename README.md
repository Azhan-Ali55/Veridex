# Veridex

A search engine built with C++ and Python, indexing the U.S. Federal Register.

## Components

| Component | Source | What it does | Output |
|---|---|---|---|
| Downloader | `tools/download_fr.py` | Fetches Federal Register documents | `data/docs.jsonl` |
| Lexicon builder | `engine/lexicon.cpp` | Maps each word to a wordID | `lexicon.txt` |
| Forward index builder | `engine/forward_index.cpp` | Maps each docID to a document record | `forward_index.bin` |
| Inverted index builder | `engine/inverted_index.cpp` | Maps each wordID to documents containing that word, with frequencies and positions | `inverted_index.bin` |

## Quick start

All commands run from the repo root. The examples use the included 500-document sample (`sample/sample_500.jsonl`).

### Lexicon

```bash
g++ -std=c++11 -O2 engine/main_lexicon.cpp engine/lexicon.cpp -o engine/lexicon.exe
./engine/lexicon.exe sample/sample_500.jsonl lexicon.txt
```

Output: `lexicon.txt`, one `wordID<TAB>word` per line. Tokenizer rules are in [DESIGN.md](DESIGN.md).

**Full dataset (48,341 documents):** 361,353 unique words, 40 s, 124 MB peak RAM.

### Forward index

```bash
g++ -std=c++11 -O2 engine/main_forward.cpp engine/forward_index.cpp engine/lexicon.cpp -o engine/forward_index.exe
./engine/forward_index.exe sample/sample_500.jsonl forward_index.bin
```

Output: `forward_index.bin`, one record per document in docID order.

### Inverted index

```bash
g++ -std=c++11 -O2 engine/main_inverted.cpp engine/inverted_index.cpp engine/forward_index.cpp engine/lexicon.cpp -o engine/inverted_index.exe
./engine/inverted_index.exe forward_index.bin lexicon.txt sample/sample_500.jsonl inverted_index.bin
```

Output: `inverted_index.bin`, one posting list per wordID. The lexicon and forward index must be generated from the same dataset before building the inverted index.

## Forward index

Maps each document to the words it contains and how often (docID -> `(wordID, tf)`). It is the input for building the inverted index. It stores no titles, dates or agencies; those go in a separate document store.

**Contents per document:** a list of `(wordID, tf)` pairs sorted by wordID, and the document length in tokens (sum of its `tf` values). It indexes the same fields as the lexicon: title, abstract and cleaned text.

**docID rule:** docIDs are assigned to valid JSON lines in file order (0, 1, 2, ...). Blank and malformed lines are skipped and do not consume a docID. The inverted indexer and the document store must follow the same rule.

**File format (`forward_index.bin`):**
- Versioned, little-endian binary with magic `FWDIDX03`
- Header, then a per-document table, then fixed-size `(wordID, tf)` entries
- Saved via a temp file and rename
- `load()` rejects wrong-magic, truncated, over-long or inconsistent files, and leaves the existing index untouched

**Size:** about 22 million entries and a 177 MB file for the full 48k-document dataset (estimated from the sample repeated to full size).

**Build** (from the repo root; the lexicon must be built first from the same corpus file):

```
g++ -std=c++11 -O2 engine/main_forward.cpp engine/forward_index.cpp engine/lexicon.cpp -o engine/forward_index.exe
engine/forward_index.exe sample/sample_500.jsonl output/lexicon.txt output/forward_index.bin
```

See `DESIGN.md` for the exact file layout.

## Inverted index

Maps each word to the documents containing it (wordID -> posting list). It is built directly from the forward index and uses the original documents to recover word positions and field-specific frequencies.

**Contents per word:** a list of postings sorted by docID, along with a shared vector of word positions. Each posting contains:

- `docID`: ID of the document containing the word
- `tf`: total number of occurrences of the word in the document
- `positionOffset`: starting offset of the posting's positions
- `titleTF`: number of occurrences in the title
- `abstractTF`: number of occurrences in the abstract

Body frequency is calculated using `bodyTF()` as `tf - titleTF - abstractTF`. Document frequency is the number of postings belonging to a word.

**Construction:** the forward index is traversed in docID order to generate posting lists. Memory is allocated beforehand based on the number of postings and positions required for each word. The original documents are then tokenized using the existing lexicon functions to recover positions and verify term frequencies against the forward index.

**docID rule:** follows the same rule as the forward index. Blank and malformed JSON lines are skipped and do not consume a docID.

**Word positions:** zero-based token positions across the title, abstract and cleaned text, processed in that order. Positions and field frequencies support frequency-based ranking, position-based ranking and future phrase searching.

**Data structures:**
- Vector of posting lists indexed directly by wordID
- Vector of postings sorted by docID
- Shared positions vector for each word to reduce memory allocation overhead

**File format (`inverted_index.bin`):**
- Versioned, little-endian binary with magic `INVIDX02`
- Header containing document count and vocabulary size
- Each posting list contains document frequency, position count, posting records and word positions
- Position offsets are reconstructed during loading
- Saved via a temp file before replacing the destination file
- `load()` rejects wrong-magic, truncated, over-long or inconsistent files, and leaves the existing index untouched

**Complexity:** let V be the vocabulary size, D the number of documents, P the number of forward index entries and T the total number of indexed tokens.

- Posting list construction and position allocation: O(V + D + P + T)
- Position recovery after tokenization: O(T + D + P) expected
- Posting list lookup: O(1)
- Memory usage: O(V + P + T)
- Posting list traversal: O(DF), where DF is the document frequency

**Sample dataset (500 documents):**

| Metric | Result |
|---|---:|
| Documents indexed | 500 |
| Unique words | 30,648 |
| Posting lists | 30,648 |
| Total postings | 230,561 |
| Word positions | 1,529,717 |
| Invalid JSON lines | 0 |
| Index size | 10,053,044 bytes (9.59 MiB) |

**Build** (from the repo root; the lexicon and forward index must be built first from the same corpus file):

```
g++ -std=c++11 -O2 engine/main_inverted.cpp engine/inverted_index.cpp engine/forward_index.cpp engine/lexicon.cpp -o engine/inverted_index.exe
engine/inverted_index.exe output/forward_index.bin output/lexicon.txt sample/sample_500.jsonl output/inverted_index.bin
```

**Build using Visual Studio (MSVC):**

```powershell
cl /EHsc /O2 /std:c++14 engine\main_inverted.cpp engine\inverted_index.cpp engine\forward_index.cpp engine\lexicon.cpp /Fe:engine\inverted_index.exe
.\engine\inverted_index.exe forward_index.bin lexicon.txt sample\sample_500.jsonl inverted_index.bin
```

**Testing:**

The inverted index is tested against the forward index and original documents to verify term frequencies, word positions, field frequencies and binary loading. Corrupted files are also tested to ensure invalid indexes are rejected.

```bash
g++ -std=c++11 -O2 -Iengine tests/test_inverted_index.cpp engine/inverted_index.cpp engine/forward_index.cpp engine/lexicon.cpp -o engine/test_inverted.exe
./engine/test_inverted.exe forward_index.bin lexicon.txt sample/sample_500.jsonl inverted_index.bin
```

Using Visual Studio (MSVC):

```powershell
cl /EHsc /O2 /std:c++14 /Iengine tests\test_inverted_index.cpp engine\inverted_index.cpp engine\forward_index.cpp engine\lexicon.cpp /Fe:engine\test_inverted.exe
.\engine\test_inverted.exe forward_index.bin lexicon.txt sample\sample_500.jsonl inverted_index.bin
```

**Test results:**

```
Term frequency test passed.
Word position test passed.
Field frequency test passed.
Corrupted file test passed.

All tests passed successfully!
Documents: 500
Unique words: 30648
Verified postings: 230561
Verified positions: 1529717
```

The inverted index has been compiled and tested using GCC and Microsoft Visual C++ against the included 500-document sample. Full-dataset performance and memory usage are yet to be benchmarked.

## Data

Source: [FederalRegister.gov API](https://www.federalregister.gov/developers/documentation/api/v1) (Office of the Federal Register, NARA), public domain.
Downloader: `scripts/download_fr.py`
