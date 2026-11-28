# Veridex

A search engine built with C++ and Python, indexing the U.S. Federal Register.

## Components

| Component | Source | What it does | Output |
|---|---|---|---|
| Downloader | `tools/download_fr.py` | Fetches Federal Register documents | `data/docs.jsonl` |
| Lexicon builder | `engine/lexicon.cpp` | Maps each word to a wordID | `lexicon.txt` |
| Forward index builder | `engine/forward_index.cpp` | Maps each docID to a document record | `forward_index.bin` |

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

## Data

Source: [FederalRegister.gov API](https://www.federalregister.gov/developers/documentation/api/v1) (Office of the Federal Register, NARA), public domain.
Downloader: `scripts/download_fr.py`
