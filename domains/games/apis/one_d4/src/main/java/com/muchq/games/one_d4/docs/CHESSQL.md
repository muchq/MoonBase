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

The `motif()` function checks for tactical pattern presence. Queries compile to `EXISTS` subqueries
against the `motif_occurrences` table. Most motifs are stored directly as rows in that table;
a few are derived from `ATTACK` rows using flag or grouping conditions.

**Directly stored motifs** (one row per occurrence):

| ChessQL                       | motif_occurrences filter        |
|-------------------------------|---------------------------------|
| `motif(pin)`                  | `motif = 'PIN'`                 |
| `motif(cross_pin)`            | `motif = 'CROSS_PIN'`           |
| `motif(skewer)`               | `motif = 'SKEWER'`              |
| `motif(attack)`               | `motif = 'ATTACK'`              |
| `motif(check)`                | `motif = 'CHECK'`               |
| `motif(promotion)`            | `motif = 'PROMOTION'`           |
| `motif(promotion_with_check)` | `motif = 'PROMOTION_WITH_CHECK'`|
| `motif(promotion_with_checkmate)` | `motif = 'PROMOTION_WITH_CHECKMATE'` |
| `motif(back_rank_mate)`       | `motif = 'BACK_RANK_MATE'`      |
| `motif(smothered_mate)`       | `motif = 'SMOTHERED_MATE'`      |
| `motif(sacrifice)`            | `motif = 'SACRIFICE'`           |
| `motif(zugzwang)`             | `motif = 'ZUGZWANG'`            |
| `motif(interference)`         | `motif = 'INTERFERENCE'`        |
| `motif(overloaded_piece)`     | `motif = 'OVERLOADED_PIECE'`    |

**Derived motifs** (computed from `ATTACK` rows):

| ChessQL                 | Derivation condition |
|-------------------------|----------------------|
| `motif(discovered_attack)` | `motif = 'ATTACK' AND is_discovered = TRUE` |
| `motif(checkmate)`      | `motif = 'ATTACK' AND is_mate = TRUE` |
| `motif(discovered_check)` | `motif = 'ATTACK' AND is_discovered = TRUE AND target LIKE 'K%' OR 'k%'` |
| `motif(fork)`           | `motif = 'ATTACK' AND is_discovered = FALSE AND attacker IS NOT NULL`, grouped by `(ply, attacker)` with `HAVING COUNT(*) >= 2` |
| `motif(double_check)`   | `motif = 'ATTACK' AND target IS king`, grouped by `ply` with `HAVING COUNT(*) >= 2` |

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

| ChessQL Input | SQL Output (WHERE clause fragment) | Parameters |
|---------------|-----------------------------------|------------|
| `white.elo >= 2500` | `white_elo >= ?` | `[2500]` |
| `motif(pin)` | `EXISTS (SELECT 1 FROM motif_occurrences mo WHERE mo.game_url = g.game_url AND mo.motif = 'PIN')` | `[]` |
| `motif(checkmate)` | `EXISTS (SELECT 1 FROM motif_occurrences mo WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK' AND mo.is_mate = TRUE)` | `[]` |
| `white.elo >= 2500 AND motif(pin)` | `(white_elo >= ? AND EXISTS (...motif = 'PIN'...))` | `[2500]` |
| `NOT motif(pin)` | `(NOT EXISTS (...motif = 'PIN'...))` | `[]` |
| `platform IN ["lichess", "chess.com"]` | `LOWER(platform) IN (LOWER(?), LOWER(?))` | `["lichess", "chess.com"]` |

## Error Handling

- **Unknown field**: `IllegalArgumentException` — "Unknown field: X"
- **Unknown motif**: `IllegalArgumentException` — "Unknown motif: X"
- **Syntax error**: `ParseException` — includes position information
- **Unterminated string**: `IllegalArgumentException` — includes position
- **Unexpected token**: `ParseException` — includes token and position

## Security

All values are bound as JDBC parameters (`?`), never interpolated into SQL strings. Field names and motif names are validated against a whitelist before being included in SQL. The compiler rejects any unrecognized identifiers.
