# UniCo language specification (compact, for humans and AI models)

This file is the complete reference for writing UniCo programs. It is meant
to be pasted into the context of an AI model that has never seen UniCo.
Version: 0.3.0. File extension: `.uc`. Run with `unico file.uc`.

## 1. Lexical rules

- Comments: `#` or `//` to end of line.
- Statements are separated by newlines or `;`. A newline does **not** end a
  statement after a binary operator, `,`, `(`, `[`, `{`, `=`, `=>`, `|>`, `.`,
  and is ignored inside `( )` and `[ ]`. A line starting with `|>` or `.name`
  continues the previous line.
- Identifiers: letters, digits, `_`, and any UTF-8 letter (`età` is valid).
- Numbers: `42`, `3.14`, `1_000`, `0xFF`, `1e-3`. All numbers are doubles.
- Strings: `"..."` with interpolation `{expr}`; `'...'` without interpolation.
  Escapes in both: `\n \t \r \0 \\ \" \' \{ \}`.
  **A literal `{` inside `"..."` must be written `\{`.** To write JSON by hand
  use single quotes: `'{"a": 1}'`.

## 2. Keywords (English and Italian are interchangeable)

| English | Italian |
|---|---|
| `if` `else` | `se` `altrimenti` |
| `while` | `mentre` |
| `for` `in` | `per` `in` |
| `match` | `scegli` |
| `fn` | `fn` `funz` |
| `return` `break` `continue` | `ritorna` `basta` `continua` |
| `true` `false` `nil` | `vero` `falso` `nulla` |
| `and` `or` `not` | – – `non` |

Keywords are reserved in both languages: `se`, `per`, `in`, `non`, `basta`,
`vero`, `nulla`… cannot be used as variable names.

## 3. Values

`nil`, booleans, numbers, strings (immutable, UTF-8), **cells**, functions.

Truthiness: only `false` and `nil` are false. **`0`, `""` and `[]` are true.**

### Cells

A cell is the only data structure: an ordered list part plus a keyed part.

```uc
[1, 2, 3]                 # list
[name: "Ada", age: 36]    # dictionary / object
[1, 2, color: "blue"]     # both
["two words": 1, 5: "x"]  # string and number keys
[]                        # empty
```

- `c[i]` indexes the list part (negative = from the end); `c.key` / `c["key"]`
  read the keyed part. Missing index or key → `nil` (no error).
- `c[len_of_list] = v` appends; `c.key = v` adds or updates a key.
- `len(c)` = list items + keys.
- Cells are mutable and passed **by reference**. `copy(c)` makes a shallow copy.
- `a + b` on cells returns a new cell: lists concatenated, keys merged (b wins).
- `==` compares cells deeply.

## 4. Operators (from lowest to highest precedence)

| Precedence | Operators |
|---|---|
| 1 | `=` `+=` `-=` `*=` `/=` (right-assoc) |
| 2 | `\|>` (pipe) |
| 3 | `??` (use right side if left is nil) |
| 4 | `or` `\|\|` |
| 5 | `and` `&&` |
| 6 | `==` `!=` `<` `<=` `>` `>=` `in` `not in` |
| 7 | `` `f` `` (infix call) |
| 8 | `..` (inclusive range) `..<` (exclusive range) |
| 9 | `+` `-` |
| 10 | `*` `/` `%` |
| 11 | unary `-` `!` `not` |
| 12 | call `f(x)`, index `a[i]`, field `a.b` |

- **`|>` has the lowest precedence**: write `(xs |> sum) == 10`, not
  `xs |> sum == 10`.
- `+` with a string on either side concatenates (`"a" + 1 == "a1"`).
- `str * n` repeats a string, `cell * n` repeats a list.
- `%` result has the sign of the divisor (`-7 % 3 == 2`). `/` is float division;
  use `floor(a / b)` for integer division.
- `<` etc. compare numbers with numbers and strings with strings; mixing
  types is an error.
- `and`/`or` return one of their operands (`nil or "x"` → `"x"`).
- `x in c`: substring for strings; for cells, a list item equal to `x` or a key `x`.
- Ranges `a..b` / `a..<b` build a list of numbers (descending if `a > b`).

## 5. Variables and scope

- `x = v` creates or updates `x` **in the current function**. It never
  modifies a variable of an enclosing function or the global scope.
- Reading `x` searches the current function, then enclosing functions, then globals.
- `^x = v` (also `^x += v`) updates the nearest **enclosing** variable `x`.
- Using a name that was never assigned is an error (also with `??`).
- Only functions create scopes; `if`/`while`/blocks do not. Variables of a
  `for` loop are visible after the loop, but each iteration gets its own
  copy for closures created in the body.

```uc
total = 0
fn add(n) { ^total += n }      # without ^ it would create a local "total"
```

## 6. Functions

```uc
fn square(n) = n * n               # expression body
fn greet(name) {                   # block body: value of last statement is returned
  print("hi {name}")
  name.upper()
}
f = |a, b| a + b                   # lambda
g = || 42                          # lambda without parameters
h = fn(x) { x * 2 }                # anonymous fn
return value                       # early return (also bare "return" → nil)
```

- Missing arguments are `nil`; extra arguments are ignored.
- Default values: `fn f(a, b = 10, c = a * 2)`. A default is used when the
  argument is missing **or nil**, and may refer to earlier parameters.
- Rest parameter (last only): `fn log(level, ...parts)` → `parts` is a list
  of the remaining arguments (possibly empty).
- Spread: `f(...xs)` passes the list items of `xs` as arguments;
  `[0, ...xs, ...dict]` copies items and keys into a new cell. `...nil` is empty.
- Functions are closures.
- Recursion depth limit: 1800 calls.

### Four call styles

```uc
f(x, y)
x.f(y)       # if x is a cell with field f → method call (see @); otherwise f(x, y)
x |> f(y)    # f(x, y)
x |> f       # f(x)
x `f` y      # f(x, y)
```

`x.f(...)` works on any value: `"abc".upper()`, `[1, 2].map(g)`, `n.abs()`.

### Methods and `@`

When a function stored in a cell is called as `cell.f(...)`, `@` inside it is
the cell. `@name` is short for `@.name`.

```uc
counter = [n: 0, inc: || { @n += 1; @ }]
counter.inc().inc()
print(counter.n)               # 2
dog = [name: "Rex"] + [speak: || "{@name} barks"]
```

There are no classes: build objects by extending cells with `+`.

## 7. Control flow (all are expressions)

```uc
if c { a } else if d { b } else { c }      # value of the chosen block; nil if none
while cond { ... }                          # value nil
for x in xs { ... }                         # list items, then key values
for i, x in xs { ... }                      # index/key and value
for k, v in dict { ... }
for i in 1..10 { ... }                      # range, no list allocated
for i in 5 { ... }                          # 0..4
for ch in "città" { ... }                   # UTF-8 characters
for x in gen { ... }                        # calls gen() until it returns nil
for x in nil { ... }                        # zero iterations
break   continue
```

A block `{ ... }` evaluates to its last statement.

## 8. match

```uc
result = match value {
  0                   => "zero"
  1 | 2               => "one or two"          # alternatives
  3..9                => "small"               # numeric range
  "hi"                => "greeting"
  []                  => "empty list"
  [x, y]              => "pair {x} {y}"        # exact-length list, binds x and y
  [first, _, _]       => "triple"
  [name: n, age: a]   => "{n} {a}"             # required keys (extra keys allowed)
  n if n < 0          => "negative"            # binding + guard
  _                   => "anything else"
}
```

- `_` matches anything; a bare name matches anything and binds it.
- To compare with a variable's value, use a guard: `v if v == limit => ...`.
- A list pattern without keys only matches cells with no keys.
- No arm matched → `nil`. Arms are separated by newlines or commas.
- Guards that compare mismatched types raise errors: check `type(v)` first.

## 9. Errors

```uc
error("message")                 # raise; any value can be raised
error([code: 404])
try(|| risky())                  # returns the result, or nil on error
try(|| risky(), |e| handle(e))   # handler receives the raised value
assert(cond, "message")
```

Runtime error messages are strings like `"file.uc:12: <message>"`, in the
current language. Uncaught errors print the call chain and exit with status 1.

## 10. Built-in functions

English name / Italian name. `xs` = cell, `s` = string, `f` = function.

**I/O**: `print(...)`/`stampa` (space-separated, newline) · `write(...)`/`scrivi`
(no separator, no newline) · `input(prompt?)`/`chiedi` (nil at EOF)
**Conversion**: `str(v)`/`testo` · `num(v)`/`numero` (nil if invalid) ·
`type(v)` → `"nil" "bool" "number" "string" "cell" "function"` ·
`tipo(v)` → the same in Italian: `"nulla" "logico" "numero" "testo" "cella" "funzione"`.
These never depend on the message language.
**Cells**: `len`/`lunghezza` · `push(xs, v...)`/`aggiungi` (returns xs) ·
`pop(xs)`/`togli` · `remove(xs, index_or_key)`/`rimuovi` · `keys`/`chiavi` ·
`values`/`valori` · `has(xs, key)`/`ha` · `copy`/`copia`
**Cells and strings**: `slice(x, from, to?)`/`taglia` (negative ok, end
exclusive) · `reverse`/`inverti` · `find(x, v)`/`cerca` (index or nil) ·
`sort(xs, less?)`/`ordina` (returns a new list; `less(a, b)` returns true if a
comes first)
**Strings**: `join(xs, sep?)`/`unisci` · `split(s, sep?)`/`dividi` (no sep:
whitespace; `""`: characters) · `replace(s, a, b)`/`sostituisci` ·
`trim`/`pulisci` · `upper`/`maiuscolo` · `lower`/`minuscolo` ·
`chr(n)`/`carattere` · `ord(s)`/`codice`
**Functional** (written in UniCo): `map(xs, f)`/`mappa` · `filter`/`filtra` ·
`reduce(xs, f, init)`/`riduci` · `each`/`ognuno` · `sum`/`somma` ·
`count(xs, f)`/`conta` · `all`/`tutti` · `any`/`qualcuno` · `zip`/`accoppia` ·
`times(n, f)`/`volte` (list of `f(0)..f(n-1)`)
**Math**: `floor`/`intero` `ceil` `round`/`arrotonda` `abs`/`assoluto`
`sqrt`/`radice` `pow`/`potenza` `sin cos tan log exp` `min`/`minimo`
`max`/`massimo` (several args or one list) · `random()`/`caso` ([0,1)),
`random(n)` (0..n-1), `random(a, b)` (a..b) · `PI`
**JSON**: `to_json(v, indent?)`/`in_json` · `from_json(s)`/`da_json`.
Cells without keys become arrays, others become objects; functions cannot
be converted; `nil` ↔ `null`.
**Time**: `clock()`/`orologio` (CPU seconds) · `time()`/`ora` (Unix seconds)
**Files and code**: `read_file(path)`/`leggi_file` (nil if missing) ·
`write_file(path, data, append?)`/`scrivi_file` · `import(path)`/`importa` ·
`eval(code)`/`esegui` · `exit(code?)`/`esci`
**Language**: `language()`/`lingua()` returns `"en"` or `"it"`;
`language("it")` switches messages and printing.
**Globals**: `ARGS`/`ARGOMENTI` (command-line arguments), `VERSIONE`.

## 11. Modules

`import("lib/geo")` runs `lib/geo.uc` (the `.uc` is optional) and returns a
cell with all variables it defined. Paths are resolved relative to the
importing file first, then to the current directory. Each module runs once;
later imports return the same cell.

## 12. Language of messages and printing

UniCo speaks English and Italian. The language changes error messages and
how `true`/`false`/`nil` print (`vero`/`falso`/`nulla` in Italian). It is
chosen by, in order: `--lang en|it`, the `UNICO_LANG` environment variable,
the system locale (`LANG`); the default is English. A program can fix it
with `language("en")` at the top, so its output is the same everywhere.

`print` shows strings raw and other values as literals: `[1, "a", k: nil]`.

## 13. Running untrusted code

```sh
unico --sandbox --steps 1000000 --time 2 --memory 64 program.uc
```

- `--sandbox` / `--protetto`: `read_file`, `write_file`, `import`, `exit` raise errors.
- `--steps N`: maximum number of executed statements.
- `--time S`: maximum CPU seconds. `--memory M`: approximate memory limit in MB.
- Limit errors cannot be caught by `try`.
- `-e CODE` runs code; `-` reads the program from standard input.
- `--lang en|it` sets the language of messages.

## 14. Common mistakes

1. `xs |> sum == 6` → write `(xs |> sum) == 6`.
2. Updating an outer variable without `^` creates a local one instead.
3. `if count { ... }` is always true when `count` is a number (0 is true).
4. `"{"` → write `"\{"` or `'{'`.
5. `match` patterns: a bare name binds, it does not compare. Use a guard.
6. `type()` returns English names (`"number"`, `"string"`, `"cell"`),
   `tipo()` Italian ones (`"numero"`, `"testo"`, `"cella"`). Do not mix them.
7. Using an undefined name is an error; assign it first (e.g. `x = nil`).
8. No `++`, no `?:` ternary (use `if` as an expression), no classes, no `let`/`var`.
9. `str(true)` is `"true"` in English and `"vero"` in Italian: call
   `language(...)` first if a program depends on it.

## 15. Complete example

```uc
fn word_count(text) {
  counts = []
  for w in text.lower().split() {
    counts[w] = (counts[w] ?? 0) + 1
  }
  counts
}

stats = word_count("the cat and the hat and the bat")
top = keys(stats)
  |> sort(|a, b| stats[a] > stats[b])
  |> slice(0, 2)
for w in top { print("{w}: {stats[w]}") }
print(to_json(stats))
```
