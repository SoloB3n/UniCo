# UniCo — un codice unico

**UniCo** è un linguaggio di programmazione piccolo, flessibile e leggerissimo.
L'interprete completo sta in **un solo file C** (`unico.c`, circa 2200 righe),
non dipende da niente oltre alla libreria standard e produce un eseguibile di
circa **60 KB**.

È "unico" in tutti i sensi:

| | |
|---|---|
| **Una sola struttura dati** | La *cella* `[ ]` fa da lista, dizionario, oggetto e modulo. |
| **Una sola regola** | Tutto è un'espressione: `se`, `scegli`, i blocchi `{ }`… restituiscono tutti un valore. |
| **Una sola lingua?** | No, due: ogni parola chiave esiste in italiano **e** in inglese, e si possono mescolare. |
| **Un solo modo di chiamare?** | No, quattro: `f(x)`, `x.f()`, `x \|> f`, ``a `f` b``. |
| **Un solo file** | Si compila ovunque ci sia un compilatore C e si incorpora in altri programmi. |

```uc
persone = [
  [nome: "Ada",   anni: 36],
  [nome: "Alan",  anni: 41],
  [nome: "Grace", anni: 85],
]

persone
  |> filtra(|p| p.anni > 40)
  |> mappa(|p| "{p.nome} ({p.anni})")
  |> unisci(", ")
  |> stampa            # Alan (41), Grace (85)
```

---

## Installazione

```sh
cc -O2 -o unico unico.c -lm      # oppure: make
./unico esempi/celle.uc          # esegue un programma
./unico                          # modalità interattiva (REPL)
./unico -e 'stampa(6 * 7)'       # esegue una riga
make test                        # esegue la suite di test
```

`make piccolo` produce un binario ottimizzato per dimensione.

---

## Il linguaggio in 5 minuti

### Valori

```uc
42  3.14  1_000_000  0xFF        # numeri
"ciao {nome}"                    # testo con interpolazione: {qualsiasi espressione}
'nessuna {interpolazione}'       # testo letterale
vero  falso  nulla               # oppure: true false nil
[1, 2, 3]                        # cella usata come lista
[nome: "Ada", anni: 36]          # cella usata come dizionario/oggetto
[1, 2, colore: "blu"]            # entrambe le cose insieme
|x| x * 2                        # funzione (lambda)
1..5   0..<5                     # intervalli (incluso / escluso)
```

Sono falsi solo `falso` e `nulla`: `0` e `""` sono veri.

### Variabili

```uc
x = 10          # crea o aggiorna una variabile della funzione corrente
x += 5          # anche -=, *=, /=
```

Un assegnamento crea **sempre** una variabile locale alla funzione: non si
modifica mai per sbaglio una variabile esterna. Per modificarla davvero si
scrive `^` davanti al nome:

```uc
fn contatore() {
  n = 0
  || { ^n += 1; n }      # ^n = la "n" della funzione esterna
}
c = contatore()
c(); c()
stampa(c())              # 3
```

### Funzioni

```uc
fn quadrato(n) = n * n               # forma breve
fn saluta(nome) {                    # forma a blocco: restituisce l'ultima espressione
  stampa("ciao {nome}")
  nome.maiuscolo()
}
doppio = |x| x * 2                   # lambda
somma3 = |a, b, c| a + b + c
fn presto(n) { se n < 0 { ritorna "negativo" }; "ok" }
```

Gli argomenti mancanti valgono `nulla`.

**Quattro modi di chiamare la stessa funzione:**

```uc
somma(mappa(lista, doppio))          # classico
lista.mappa(doppio).somma()          # a catena: x.f(a) diventa f(x, a)
lista |> mappa(doppio) |> somma      # a tubo:   x |> f(a) diventa f(x, a)
3 `massimo` 7                        # infisso:  a `f` b diventa f(a, b)
```

La catena `x.f()` funziona su **qualsiasi** valore (anche testi e numeri): se `x`
è una cella con un campo `f`, chiama quello; altrimenti cerca la funzione `f`.
L'operatore `|>` ha la precedenza più bassa: `(a |> f) == 3`.

### Celle e oggetti

```uc
c = [10, 20, nome: "x"]
c[0]  c[-1]  c.nome  c["nome"]       # lettura (indice mancante: nulla)
c[2] = 30                            # aggiunge in coda
c.colore = "blu"                     # aggiunge una chiave
lunghezza(c)                         # 5
[1, 2] + [3]                         # [1, 2, 3]
[a: 1] + [a: 2, b: 3]                # [a: 2, b: 3]
2 in [1, 2]   "a" in [a: 1]          # vero vero
```

Le celle si passano per riferimento: usa `copia(c)` se ti serve un duplicato.

Un **oggetto** è una cella che contiene funzioni. Dentro un metodo, `@` è la
cella stessa e `@campo` è un'abbreviazione di `@.campo`:

```uc
conto = [
  saldo: 0,
  deposita: |q| { @saldo += q; @ },    # restituire @ permette di concatenare
]
conto.deposita(50).deposita(25)
stampa(conto.saldo)                   # 75
```

Non ci sono classi: si **estende** una cella con `+`.

```uc
animale = [verso: "...", parla: || "{@nome} dice {@verso}"]
cane    = animale + [nome: "Fido", verso: "bau"]
cane.parla()                          # "Fido dice bau"
```

### Controllo del flusso

```uc
se x > 0 { "positivo" } altrimenti se x < 0 { "negativo" } altrimenti { "zero" }
y = se pronto { 1 } altrimenti { 2 }        # se è un'espressione

mentre n > 0 { n -= 1 }

per i in 1..10 { ... }               # intervallo (anche al contrario: 10..1)
per x in lista { ... }               # valori
per i, x in lista { ... }            # indice e valore
per k, v in mappa { ... }            # chiave e valore
per car in "città" { ... }           # caratteri (UTF-8)
per i in 5 { ... }                   # 0, 1, 2, 3, 4
per x in generatore { ... }          # chiama la funzione finché restituisce nulla
basta      continua                  # break / continue
```

### `scegli`: pattern matching e destrutturazione

```uc
fn descrivi(v) = scegli v {
  0                     => "zero"
  1 | 2 | 3             => "piccolo"
  4..9                  => "medio"
  []                    => "lista vuota"
  [x, y]                => "coppia con somma {x + y}"
  [nome: n, anni: a]    => "{n} ha {a} anni"
  k se tipo(k) == "numero" and k < 0 => "negativo"
  _                     => "altro"
}
```

Nei pattern, `_` accetta tutto, un nome nudo cattura il valore e
`se condizione` aggiunge una guardia.

### Errori

```uc
risultato = prova(|| {
  se b == 0 { errore("divisione per zero") }    # errore() accetta qualsiasi valore
  a / b
}, |e| "impossibile: {e}")
```

### Moduli

Ogni file è un modulo: `importa` lo esegue e restituisce le sue variabili come
cella.

```uc
geo = importa("geometria")        # carica geometria.uc
geo.area_cerchio(2)
```

---

## Parole chiave

| Italiano | English | |
|---|---|---|
| `se` / `altrimenti` | `if` / `else` | condizione |
| `mentre` | `while` | ciclo |
| `per … in` | `for … in` | iterazione |
| `scegli` | `match` | pattern matching |
| `fn`, `funz` | `fn` | funzione |
| `ritorna` | `return` | |
| `basta` / `continua` | `break` / `continue` | |
| `vero` / `falso` / `nulla` | `true` / `false` / `nil` | |
| `non` | `not` | anche `!` |
| | `and` / `or` | anche `&&` / `\|\|` |

Operatori: `+ - * / %` · `== != < <= > >=` · `in`, `not in` · `??` (valore
predefinito se nulla) · `..` `..<` · `|>` · `` `f` `` · `^nome` · `@` `@campo`.
I commenti iniziano con `#` oppure `//`.

## Funzioni incluse

Ogni funzione ha un nome italiano e uno inglese.

| Italiano | English | |
|---|---|---|
| `stampa`, `scrivi`, `chiedi` | `print`, `write`, `input` | ingresso/uscita |
| `testo`, `numero`, `tipo` | `str`, `num`, `type` | conversioni |
| `lunghezza` | `len` | |
| `aggiungi`, `togli`, `rimuovi`, `copia` | `push`, `pop`, `remove`, `copy` | celle |
| `chiavi`, `valori`, `ha` | `keys`, `values`, `has` | |
| `taglia`, `inverti`, `ordina`, `cerca` | `slice`, `reverse`, `sort`, `find` | celle e testi |
| `unisci`, `dividi`, `sostituisci`, `pulisci` | `join`, `split`, `replace`, `trim` | testi |
| `maiuscolo`, `minuscolo` | `upper`, `lower` | |
| `carattere`, `codice` | `chr`, `ord` | |
| `mappa`, `filtra`, `riduci`, `ognuno` | `map`, `filter`, `reduce`, `each` | funzionali |
| `somma`, `conta`, `tutti`, `qualcuno` | `sum`, `count`, `all`, `any` | |
| `accoppia`, `volte` | `zip`, `times` | |
| `intero`, `arrotonda`, `assoluto`, `radice`, `potenza` | `floor`, `round`, `abs`, `sqrt`, `pow` | matematica |
| `minimo`, `massimo`, `caso` | `min`, `max`, `random` | anche `ceil sin cos tan log exp`, `PI` |
| `orologio`, `ora` | `clock`, `time` | tempo |
| `errore`, `prova`, `verifica` | `error`, `try`, `assert` | errori |
| `leggi_file`, `scrivi_file` | `read_file`, `write_file` | file |
| `importa`, `esegui`, `esci` | `import`, `eval`, `exit` | |

Gli argomenti da riga di comando sono in `ARGOMENTI` (`ARGS`).
Le funzioni funzionali (`mappa`, `filtra`, …) sono scritte in UniCo stesso.

---

## Incorporare UniCo in un programma C

```c
#define UNICO_NO_MAIN
#include "unico.c"

static Value raddoppia(int argc, Value *argv) {
    return vnum(argc ? argv[0].u.n * 2 : 0);
}

int main(void) {
    uc_init();
    uc_def("raddoppia", raddoppia);
    const char *codice = "stampa(raddoppia(21))";
    uc_run(codice, strlen(codice), "incorporato", NULL);
}
```

## Com'è fatto

- **Lexer → parser → albero sintattico**, valutato direttamente (interprete
  ad albero): niente bytecode, quindi codice semplice da leggere e modificare.
- **Garbage collector mark & sweep** che gira solo tra un'istruzione e l'altra.
  `make stress` lo esegue a *ogni* istruzione sotto AddressSanitizer per
  verificarne la correttezza.
- **Le variabili stanno in celle**: un modulo importato è semplicemente
  l'ambiente in cui è stato eseguito.
- Nomi e testi del sorgente sono condivisi in un'unica tabella, così i
  confronti tra nomi costano poco.

## Esempi

Nella cartella [`esempi/`](esempi):

- `ciao.uc`: il primo programma
- `bilingue.uc`: FizzBuzz in italiano e in inglese, con `scegli`
- `celle.uc`: liste, dizionari, oggetti, estensione
- `funzioni.uc`: catene, tubi, infissi, closure, generatori
- `vita.uc`: il gioco della vita di Conway
- `moduli.uc`: `importa` e gestione degli errori
