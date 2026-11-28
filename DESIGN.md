# Veridex design

## Pipeline
- `docs.jsonl` means the input corpus file you pass in (e.g. `data/docs.jsonl` or `sample/sample_500.jsonl`).
- docs.jsonl -> Lexicon (word -> wordID) -> `lexicon.txt`
- docs.jsonl + lexicon -> ForwardIndex (docID -> wordID, tf) -> `forward_index.bin`
- Build the lexicon first: the forward index resolves every word through it.
- Both must be built from the SAME corpus file.

## docIDs (shared by every component)
- docIDs are 0,1,2,... assigned to valid JSON objects in file order.
- Blank lines and invalid JSON lines are skipped and do NOT consume a docID.
- Every component that numbers documents must use this exact rule.

## Lexicon

### Tokenizer (one shared function: `Lexicon::tokenize`)
- Lowercase; a word is a run of letters/digits (a-z, 0-9); everything else is a separator.
- Drop tokens shorter than 2 or longer than 30 characters.
- NO stopword removal (the spec requires queries like "data structures and algorithms" to match "and").
- Index title, abstract and text. Strip the boilerplate header at the start of `text` (`Lexicon::cleanBody`).
- A missing, null or non-string field counts as an empty string.
- The query side must call the same tokenizer.

### IDs
- wordID: 0,1,2,... in order of first appearance.

### File
- lexicon.txt: `wordID<TAB>word`
- IDs must be 0..N-1 in order; empty or duplicate words are rejected on load.

## Forward index

### Content
- For each docID: a list of `(wordID, tf)` pairs, sorted by wordID, each wordID once, tf >= 1.
- For each docID: document length in tokens = sum of its tf values (used for ranking).
- Indexes exactly what the lexicon indexes (title, abstract, cleaned text).
- Stores no metadata (title, date, ...); that belongs to a separate document store.
- A token missing from the lexicon means the lexicon is stale; the driver refuses to save.

### File
- forward_index.bin, little-endian:
  - magic `FWDIDX03` (8 bytes)
  - docCount (uint32)
  - entryCount (uint64)
  - doc table: per document `termCount` (uint32), `length` (uint32)
  - entries: per document, in docID order, `wordID` (uint32), `tf` (uint32)
- File size is fixed by docCount and entryCount; load() checks it before allocating.
- load() rejects the file if wordIDs are not ascending, tf is 0, or length != sum of tf. The existing index is left untouched.
- save() writes `<path>.tmp` and then moves it into place.
- Change the magic whenever the layout changes.
