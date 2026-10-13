# UniCo — one code, unique

**UniCo** is a small, flexible and extremely lightweight programming language.
The whole interpreter lives in **a single C file** (`unico.c`, about 2200 lines),
depends on nothing but the C standard library, and builds into a binary of
about **60 KB**.

It is "unique" (*unico* in Italian) in every sense:

| | |
|---|---|
| **One data structure** | The *cell* `[ ]` is a list, a dictionary, an object and a module. |
| **One rule** | Everything is an expression: `if`, `match`, blocks `{ }`… all return a value. |
| **One language?** | No, two: every keyword and built-in exists in **English and Italian**, and you can mix them. |
| **One way to call?** | No, four: `f(x)`, `x.f()`, `x \|> f`, ``a `f` b``. |
| **One file** | Compiles anywhere there is a C compiler, and embeds into other programs. |
| **AI-ready** | A compact [`SPEC.md`](SPEC.md) teaches the language to an AI model, and a sandbox with step, time and memory limits runs untrusted code safely. |

```uc
people = [
  [name: "Ada",   age: 36],
  [name: "Alan",  age: 41],
  [name: "Grace", age: 85],
]

people
  |> filter(|p| p.age > 40)
  |> map(|p| "{p.name} ({p.age})")
  |> join(", ")
  |> print            # Alan (41), Grace (85)
```

---

## Getting started

```sh
cc -O2 -o unico unico.c -lm      # or: make
./unico esempi/celle.uc          # run a program
./unico                          # interactive mode (REPL)
./unico -e 'print(6 * 7)'        # run a one-liner
make test                        # run the test suites
```

`make piccolo` builds a size-optimized binary.

### Command-line options

```sh
unico [options] [file.uc [args...]]
  -e CODE                  run CODE
  -                        read the program from standard input
  --sandbox, --protetto    disable files, import() and exit()
  --steps N, --passi N     stop after N executed statements
  --time S, --tempo S      stop after S seconds of CPU time
  --memory M, --memoria M  stop above about M megabytes of memory
```

Limit errors cannot be caught by `try`, so untrusted code cannot escape them.

---

## The language in 5 minutes

### Values

```uc
42  3.14  1_000_000  0xFF        # numbers
"hello {name}"                   # string interpolation: {any expression}
'no {interpolation}'             # literal string
true  false  nil                 # or: vero falso nulla
[1, 2, 3]                        # a cell used as a list
[name: "Ada", age: 36]           # a cell used as a dictionary/object
[1, 2, color: "blue"]            # both at once
|x| x * 2                        # function (lambda)
1..5   0..<5                     # ranges (inclusive / exclusive)
```

Only `false` and `nil` are falsy: `0` and `""` are truthy.

### Variables

```uc
x = 10          # creates or updates a variable of the current function
x += 5          # also -=, *=, /=
```

An assignment **always** creates a variable local to the current function, so
you can never overwrite an outer variable by accident. To really modify one,
prefix its name with `^`:

```uc
fn counter() {
  n = 0
  || { ^n += 1; n }      # ^n = the "n" of the enclosing function
}
c = counter()
c(); c()
print(c())               # 3
```

### Functions

```uc
fn square(n) = n * n                 # short form
fn greet(name) {                     # block form: returns the last expression
  print("hello {name}")
  name.upper()
}
double = |x| x * 2                   # lambda
add3 = |a, b, c| a + b + c
fn early(n) { if n < 0 { return "negative" }; "ok" }
```

Missing arguments are `nil` (use `x = x ?? default` for default values).

**Four ways to call the same function:**

```uc
sum(map(list, double))               # classic
list.map(double).sum()               # chained: x.f(a) becomes f(x, a)
list |> map(double) |> sum           # piped:   x |> f(a) becomes f(x, a)
3 `max` 7                            # infix:   a `f` b becomes f(a, b)
```

Chaining with `x.f()` works on **any** value (strings and numbers too): if `x`
is a cell with a field `f`, that is called; otherwise the function `f` is
looked up. The `|>` operator has the lowest precedence: `(a |> f) == 3`.

### Cells and objects

```uc
c = [10, 20, name: "x"]
c[0]  c[-1]  c.name  c["name"]       # read (missing index: nil)
c[2] = 30                            # appends
c.color = "blue"                     # adds a key
len(c)                               # 5
[1, 2] + [3]                         # [1, 2, 3]
[a: 1] + [a: 2, b: 3]                # [a: 2, b: 3]
2 in [1, 2]   "a" in [a: 1]          # true true
```

Cells are passed by reference: use `copy(c)` when you need a duplicate.

An **object** is a cell that contains functions. Inside a method, `@` is the
cell itself and `@field` is shorthand for `@.field`:

```uc
account = [
  balance: 0,
  deposit: |q| { @balance += q; @ },   # returning @ allows chaining
]
account.deposit(50).deposit(25)
print(account.balance)                 # 75
```

There are no classes: you **extend** a cell with `+`.

```uc
animal = [sound: "...", speak: || "{@name} says {@sound}"]
dog    = animal + [name: "Rex", sound: "woof"]
dog.speak()                            # "Rex says woof"
```

### Control flow

```uc
if x > 0 { "positive" } else if x < 0 { "negative" } else { "zero" }
y = if ready { 1 } else { 2 }        # if is an expression

while n > 0 { n -= 1 }

for i in 1..10 { ... }               # range (also backwards: 10..1)
for x in list { ... }                # values
for i, x in list { ... }             # index and value
for k, v in dict { ... }             # key and value
for ch in "città" { ... }            # characters (UTF-8 aware)
for i in 5 { ... }                   # 0, 1, 2, 3, 4
for x in generator { ... }           # calls the function until it returns nil
break      continue
```

Every closure created inside a `for` loop remembers the value of the loop
variable *for that iteration*.

### `match`: pattern matching and destructuring

```uc
fn describe(v) = match v {
  0                     => "zero"
  1 | 2 | 3             => "small"
  4..9                  => "medium"
  []                    => "empty list"
  [x, y]                => "pair adding up to {x + y}"
  [name: n, age: a]     => "{n} is {a} years old"
  k if type(k) == "numero" and k < 0 => "negative"
  _                     => "something else"
}
```

In patterns, `_` matches anything, a bare name captures the value, and
`if condition` adds a guard.

### Errors

```uc
result = try(|| {
  if b == 0 { error("division by zero") }    # error() accepts any value
  a / b
}, |e| "impossible: {e}")
```

Uncaught errors show the file, the line and the chain of calls:

```
errore: lib/util.uc:4: operazione '+' non valida tra numero e nulla
  in rompi, chiamata da main.uc:5
  in interna, chiamata da main.uc:4
```

### JSON

```uc
data = from_json('{"name": "Ada", "langs": ["C", "UniCo"]}')
print(data.langs[1])                  # UniCo
print(to_json(data))                  # compact
print(to_json(data, 2))               # indented
```

Cells without keys become JSON arrays, the others become objects. Inside
`"..."` a `{` starts an interpolation, so write hand-made JSON between
single quotes `'...'` (or escape the brace as `\{`).

### Modules

Every file is a module: `import` runs it and returns its variables as a cell.

```uc
geo = import("geometry")          # loads geometry.uc
geo.circle_area(2)
```

Paths are resolved relative to the importing file first, then to the current
directory. A module runs only once: later imports return the same cell.

---

## Keywords

| English | Italian | |
|---|---|---|
| `if` / `else` | `se` / `altrimenti` | condition |
| `while` | `mentre` | loop |
| `for … in` | `per … in` | iteration |
| `match` | `scegli` | pattern matching |
| `fn` | `fn`, `funz` | function |
| `return` | `ritorna` | |
| `break` / `continue` | `basta` / `continua` | |
| `true` / `false` / `nil` | `vero` / `falso` / `nulla` | |
| `not` | `non` | also `!` |
| `and` / `or` | | also `&&` / `\|\|` |

Operators: `+ - * / %` · `== != < <= > >=` · `in`, `not in` · `??` (default
value when nil) · `..` `..<` · `|>` · `` `f` `` · `^name` · `@` `@field`.
Comments start with `#` or `//`.

## Built-in functions

Every function has an English and an Italian name.

| English | Italian | |
|---|---|---|
| `print`, `write`, `input` | `stampa`, `scrivi`, `chiedi` | input/output |
| `str`, `num`, `type` | `testo`, `numero`, `tipo` | conversions |
| `len` | `lunghezza` | |
| `push`, `pop`, `remove`, `copy` | `aggiungi`, `togli`, `rimuovi`, `copia` | cells |
| `keys`, `values`, `has` | `chiavi`, `valori`, `ha` | |
| `slice`, `reverse`, `sort`, `find` | `taglia`, `inverti`, `ordina`, `cerca` | cells and strings |
| `join`, `split`, `replace`, `trim` | `unisci`, `dividi`, `sostituisci`, `pulisci` | strings |
| `upper`, `lower` | `maiuscolo`, `minuscolo` | |
| `chr`, `ord` | `carattere`, `codice` | |
| `map`, `filter`, `reduce`, `each` | `mappa`, `filtra`, `riduci`, `ognuno` | functional |
| `sum`, `count`, `all`, `any` | `somma`, `conta`, `tutti`, `qualcuno` | |
| `zip`, `times` | `accoppia`, `volte` | |
| `floor`, `round`, `abs`, `sqrt`, `pow` | `intero`, `arrotonda`, `assoluto`, `radice`, `potenza` | math |
| `min`, `max`, `random` | `minimo`, `massimo`, `caso` | also `ceil sin cos tan log exp`, `PI` |
| `clock`, `time` | `orologio`, `ora` | time |
| `error`, `try`, `assert` | `errore`, `prova`, `verifica` | errors |
| `to_json`, `from_json` | `in_json`, `da_json` | JSON |
| `read_file`, `write_file` | `leggi_file`, `scrivi_file` | files |
| `import`, `eval`, `exit` | `importa`, `esegui`, `esci` | |

Command-line arguments are in `ARGS` (`ARGOMENTI`).
The functional helpers (`map`, `filter`, …) are written in UniCo itself.

Note: values print with Italian names (`vero`, `falso`, `nulla`), and `type()`
returns Italian type names: `"numero"`, `"testo"`, `"cella"`, `"funzione"`,
`"logico"`, `"nulla"`. Error messages are in Italian too.

---

## Embedding UniCo in a C program

```c
#define UNICO_NO_MAIN
#include "unico.c"

static Value double_it(int argc, Value *argv) {
    return vnum(argc ? argv[0].u.n * 2 : 0);
}

int main(void) {
    uc_init();
    uc_def("double_it", double_it);
    uc_sandbox(1);                        /* optional: no files, import, exit */
    uc_limits(1000000, 2.0, 64 << 20);    /* optional: steps, seconds, bytes */
    const char *code = "print(double_it(21))";
    uc_run(code, strlen(code), "embedded", NULL);
}
```

## Using UniCo with AI

- **Teaching it**: paste [`SPEC.md`](SPEC.md) into the model's context. It is
  a compact, complete reference with a list of the most common mistakes.
- **Running AI-written code**: use `--sandbox` with `--steps`, `--time` and
  `--memory` (or `uc_sandbox` and `uc_limits` when embedding). Errors report
  `file:line` and the call chain, so a model can fix its own code.
- **Exchanging data**: `to_json` and `from_json` connect UniCo to anything
  that speaks JSON.

## How it works

- **Lexer → parser → syntax tree**, evaluated directly (a tree-walking
  interpreter): no bytecode, so the code is easy to read and change.
- **Mark & sweep garbage collector** that runs only between statements, where
  the step, time and memory limits are also checked.
  `make stress` runs it on *every* statement under AddressSanitizer to check
  that it is correct.
- **Variables live in cells**: an imported module is simply the environment
  it was executed in.
- Names and strings from the source code are interned in a single table,
  so comparing names is cheap.
- Code passed to `eval` that defines no functions is freed right after it
  runs, so `eval` in a loop does not grow memory.

## Examples

In the [`esempi/`](esempi) folder:

- `ciao.uc`: the first program
- `bilingue.uc`: FizzBuzz in Italian and in English, with `match`
- `celle.uc`: lists, dictionaries, objects, extension
- `funzioni.uc`: chaining, pipes, infix calls, closures, generators
- `vita.uc`: Conway's Game of Life
- `moduli.uc`: `import` and error handling
