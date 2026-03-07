# ChessQL — Query Language Reference

## Overview

ChessQL is a domain-specific query language for searching indexed chess games. Queries are compiled to parameterized SQL at runtime — no string interpolation, no injection risk.

## Grammar (EBNF)

```
query       ::= expr EOF
expr        ::= or_expr
or_expr     ::= and_expr ("OR" and_expr)*
and_expr    ::= not_expr ("AND" not_expr)*
not_expr    ::= "NOT" not_expr | primary
primary     ::= comparison | in_expr | motif_call | "(" expr ")"
comparison  ::= field comp_op value
in_expr     ::= field "IN" "[" value_list "]"
motif_call  ::= "motif" "(" IDENTIFIER ")"
field       ::= IDENTIFIER ("." IDENTIFIER)*
comp_op     ::= "=" | "!=" | "<" | "<=" | ">" | ">="
value       ::= NUMBER | STRING
value_list  ::= value ("," value)*
```

## Operator Precedence

From lowest to highest:

1. `OR`
2. `AND`
3. `NOT` (unary prefix)
4. Parenthesized expressions

`AND` binds tighter than `OR`, so `a OR b AND c` is parsed as `a OR (b AND c)`.

## Fields

Dotted field names are mapped to database columns:

| ChessQL Field    | DB Column        | Type    |
|------------------|------------------|---------|
| `white.elo`      | `white_elo`      | INT     |
| `black.elo`      | `black_elo`      | INT     |
| `white.username` | `white_username` | VARCHAR |
| `black.username` | `black_username` | VARCHAR |
| `time.class`     | `time_class`     | VARCHAR |
| `num.moves`      | `num_moves`      | INT     |
| `game.url`       | `game_url`       | VARCHAR |
| `played.at`      | `played_at`      | TIMESTAMP |
| `eco`            | `eco`            | VARCHAR |
| `result`         | `result`         | VARCHAR |
| `platform`       | `platform`       | VARCHAR |

Underscore-separated names also work directly: `white_elo >= 2500` is equivalent to `white.elo >= 2500`.

## Motifs

The `motif()` function checks boolean columns for tactical pattern presence:

| ChessQL              | SQL                          |
|----------------------|------------------------------|
| `motif(pin)`         | `has_pin = TRUE`             |
| `motif(cross_pin)`   | `has_cross_pin = TRUE`       |
| `motif(fork)`        | `has_fork = TRUE`            |
| `motif(skewer)`      | `has_skewer = TRUE`          |
| `motif(discovered_attack)` | `has_discovered_attack = TRUE` |

## Values

- **Numbers**: integer literals, optionally negative. Examples: `2500`, `-1`, `0`
- **Strings**: double-quoted. Backslash escapes supported. Examples: `"chess.com"`, `"B90"`, `"hikaru"`

## Examples

### Simple comparisons

```
white.elo >= 2500
eco = "B90"
num.moves > 40
```

### Motif queries

```
motif(fork)
motif(cross_pin)
NOT motif(pin)
```

### Boolean combinations

```
white.elo >= 2500 AND motif(cross_pin)
motif(fork) OR motif(skewer)
motif(fork) AND NOT motif(pin)
```

### IN expressions

```
platform IN ["lichess", "chess.com"]
eco IN ["B90", "B91", "B92"]
```

### Complex queries

```
white.elo >= 2500 AND motif(fork) AND NOT motif(pin)
(motif(fork) OR motif(skewer)) AND white.elo > 2000
platform IN ["chess.com"] AND black.elo > 2700 AND motif(discovered_attack)
```

## Compilation Examples

| ChessQL Input | SQL Output | Parameters |
|---------------|-----------|------------|
| `white.elo >= 2500` | `white_elo >= ?` | `[2500]` |
| `motif(fork)` | `has_fork = TRUE` | `[]` |
| `white.elo >= 2500 AND motif(fork)` | `(white_elo >= ? AND has_fork = TRUE)` | `[2500]` |
| `motif(fork) OR motif(pin)` | `(has_fork = TRUE OR has_pin = TRUE)` | `[]` |
| `NOT motif(pin)` | `(NOT has_pin = TRUE)` | `[]` |
| `platform IN ["lichess", "chess.com"]` | `platform IN (?, ?)` | `["lichess", "chess.com"]` |
| `(motif(fork) OR motif(pin)) AND white.elo > 2000` | `((has_fork = TRUE OR has_pin = TRUE) AND white_elo > ?)` | `[2000]` |

## Error Handling

- **Unknown field**: `IllegalArgumentException` — "Unknown field: X"
- **Unknown motif**: `IllegalArgumentException` — "Unknown motif: X"
- **Syntax error**: `ParseException` — includes position information
- **Unterminated string**: `IllegalArgumentException` — includes position
- **Unexpected token**: `ParseException` — includes token and position

## Security

All values are bound as JDBC parameters (`?`), never interpolated into SQL strings. Field names and motif names are validated against a whitelist before being included in SQL. The compiler rejects any unrecognized identifiers.
