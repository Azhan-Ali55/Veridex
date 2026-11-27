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

## Forward index details

**Record fields:** `document_number`, `title`, `abstract`, `publication_date`, `type`, `agency_names`, and `bodyLength` (token count after `Lexicon::cleanBody` + `Lexicon::tokenize`).

**docID rule:** docIDs are assigned to valid JSON lines in file order (0, 1, 2, ...). Malformed lines are skipped and do **not** consume a docID. The inverted indexer must follow the same rule.

**File format:**
- Versioned, little-endian binary with magic `FWDIDX01`
- Length-prefixed strings, so loading is a buffered read plus a cursor walk, with no JSON parsing at search time
- Saved atomically (temp file + rename)
- `load()` rejects truncated, stale-magic, or trailing-byte files instead of returning a half-loaded index

**Size:** the full dataset (~48k records) stays small because body text is not stored here. It lives in the inverted index and the original `texts/<docnum>.txt` files.

## Data

Source: [FederalRegister.gov API](https://www.federalregister.gov/developers/documentation/api/v1) (Office of the Federal Register, NARA), public domain.
Downloader: `scripts/download_fr.py`
