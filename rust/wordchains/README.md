# Doublets in Rust
This is a solver for the [Doublet](https://en.wikipedia.org/wiki/Word_ladder) game invented by Lewis Carroll.

The goal is to find a sequence of words that can be transformed into one another by changing only one letter at a time. Additions and deletions are not allowed.
For example, "cold" can be turned to "warm" as follows:

```
cold
cord
corm
worm
warm 
```

or for the trickier "sponge" -> "muffin":
```
sponge
sporge
spurge
spurre
sturre
stufre
soufre
soffre
coffre
coffee
coffen
coffin
cuffin
muffin
```

What is "stufre"? [OED](https://www.oed.com/search/dictionary/?scope=Entries&q=stufre) says "variant or stiver, n. Historical. A small coin (originally silver)".

## Implementation notes
- The graph of valid transformations between words is modeled as an adjacency list.
- Currently, the dictionary used is the one from `/usr/share/dict/words`. Obviously, choice of dictionary is very important. I may include a bigger dictionary in the future.
- The word graph is rebuilt every run. I may cache it for faster startup.
- There are no tests yet.
- What is Rust?

## TODO
- Add tests
- Add dictionary path argument
- Add prebuilt dictionary + graph
- Add `--help` flag
- Interactive mode
