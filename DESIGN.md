# Lexicon design

## Tokenizer (one shared function)
- Lowercase; a word is a run of letters/digits (a-z, 0-9); everything else is a separator.
- Drop tokens shorter than 2 or longer than 30 characters.
- NO stopword removal (the spec requires queries like "data structures and algorithms" to match "and").
- Index title, abstract and text. Strip the boilerplate header at the start of `text`.

## IDs
- wordID: 0,1,2,... in order of first appearance.

## File
- lexicon.txt: `wordID<TAB>word`