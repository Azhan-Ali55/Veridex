# Veridex
Search Engine Developed Using C++ and Python 
## Lexicon builder

Builds the lexicon (word -> wordID) from a JSONL file of Federal Register documents.

**Build** (from the repo root):

```
g++ -std=c++11 -O2 engine/lexicon.cpp -o engine/lexicon.exe
```

**Run** on the included 500-document sample:

```
./engine/lexicon.exe sample/sample_500.jsonl lexicon.txt
```

Output: `lexicon.txt`, one `wordID<TAB>word` per line. See `DESIGN.md` for the tokenizer rules.

Full dataset (48,341 documents): 361,353 unique words, 40 s, 124 MB peak RAM.

Data source: FederalRegister.gov API (Office of the Federal Register, NARA), public domain.
Downloader: `python/download_fr.py`.