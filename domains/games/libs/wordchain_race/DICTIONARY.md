# Word Chains dictionary

`filtered_enable_words` supplies race validation and the graph shared by
single-player and both Mithril APIs.
The build subtracts exact lines from `archaic_variants.txt` and
`reviewed_variants.txt` from the pinned ENABLE list. `archaic_variants_test`
checks that both lists are sorted, unique, present in ENABLE, and disjoint
from SCOWL's puzzle endpoints.

## Bulk exclusions

`archaic_variants.txt` starts with the ENABLE entries of 3–8 letters in
Kaikki's English Wiktionary extraction from October 30, 2026 (dump dated
October 1). A candidate requires every English sense to be an `alt_of`
sense tagged `archaic` or `obsolete`, or a `form_of` sense linked only to
excluded forms. The compressed English JSONL snapshot has SHA-256
`4d03b6cba283744756c57ff661c2c6cbfe34fcf8ff513a966576cf7d97e91a99`.
[Kaikki English extract](https://kaikki.org/dictionary/English/index.html)

Cross-check candidates for other senses and contemporary usage. The initial
review used SCOWL common words, Princeton WordNet 3.1, LibreOffice English
spellcheck dictionaries, and wordfreq 3.1.1 (English Zipf score of 2.5 or
higher) as signals to keep entries. Confirm each inflection by its own
senses or a source that links it to the archaic form. For example,
`eftsoons` has its own adverb senses and stays playable.

Specialized forms with current dictionary support also stay playable,
including `benzole`, `lentando`, `oscule`, and `pistache`.

- [Benzole](https://www.collinsdictionary.com/dictionary/english/benzol)
- [Lentando](https://www.merriam-webster.com/dictionary/lentando)
- [Oscule](https://www.collinsdictionary.com/dictionary/english/oscule)
- [Pistache](https://www.collinsdictionary.com/dictionary/english/pistache)

## Individual exclusions

`reviewed_variants.txt` records entries reviewed individually, with a source
for each spelling or inflection.

| Entry | Evidence |
| --- | --- |
| `odyls` | Plural of the obsolete variant `odyl`, from `odyle`. [Inflection](https://scrabble.merriam.com/finder/odyls) |
| `sade` | Excluded from play. It has a dialectal verb sense and a spelling of the Hebrew letter `sadhe`. [English Wiktionary](https://en.wiktionary.org/wiki/sade#English) |
| `sall` | Historical regional form of `shall`. [Yorkshire Historical Dictionary](https://yorkshiredictionary.york.ac.uk/words/sall) |
| `strown` | Past participle of the archaic variant `strow`, from `strew`. [Collins](https://www.collinsdictionary.com/dictionary/english/strow) |
| `swinneys` | Plural of the archaic variant `swinney`, from `sweeny`. [Inflection](https://scrabble.merriam.com/finder/swinneys) |
| `woalds` | Plural of the obsolete variant `woald`, from the pigment sense of `weld`. [Inflection](https://scrabble.merriam.com/finder/woalds) |
| `yonkers` | Lowercase plural of `yonker`, an obsolete form of `younker`. [English Wiktionary](https://en.wiktionary.org/wiki/yonkers#English) |
