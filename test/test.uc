# Suite di test di UniCo — eseguila con: ./unico test/test.uc
superati = 0
fn ok(cond, nome) {
  verifica(cond, nome)
  ^superati += 1
}

# --- valori e operatori
ok(1 + 2 * 3 == 7, "precedenza")
ok(-7 % 3 == 2, "modulo sempre positivo")
ok(0.1 + 0.2 != 0.3 and str(0.1 + 0.2) == "0.30000000000000004" and str(2.5) == "2.5", "stampa esatta dei decimali")
ok(str(pow(2, 53)) == "9007199254740992" and str(1/3) == "0.3333333333333333", "stampa esatta dei numeri grandi")
ok("a" + 1 == "a1", "concatenazione")
ok("ab" * 3 == "ababab", "ripetizione")
ok((nulla ?? 5) == 5, "??")
ok((falso or "x") == "x", "or restituisce il valore")
ok(0 and vero, "0 è vero")
ok(non falso, "non")

# --- testi
nome = "Ada"
ok("ciao {nome}!" == "ciao Ada!", "interpolazione")
ok("{1 + 1}{"x"}" == "2x", "interpolazione annidata")
ok('{nome}' == "\{nome}", "apici singoli")
ok(len("città") == 5 and "città"[-1] == "à", "utf-8")
ok("città".maiuscolo() == "CITTÀ", "maiuscolo accentato")
ok(split("a, b,c", ",").map(pulisci) == ["a", "b", "c"], "dividi")
ok(cerca("mondo", "do") == 3, "cerca")
ok(sostituisci("a-b-c", "-", "+") == "a+b+c", "sostituisci")
ok(inverti("abc") == "cba", "inverti testo")

# --- celle
c = [1, 2, 3, chiave: "v"]
ok(len(c) == 4 and c[0] == 1 and c.chiave == "v" and c["chiave"] == "v", "cella mista")
c[3] = 4
ok(c[3] == 4 and c.chiave == "v", "aggiunta per indice")
ok([1, 2] + [3] == [1, 2, 3], "unione liste")
ok(([a: 1] + [a: 2, b: 3]).a == 2, "unione mappe")
ok(2 in [1, 2] and "a" in [a: 1] and not (5 in [1]), "in")
ok(rimuovi(c, 0) == 1 and c[0] == 2, "rimuovi indice")
ok(rimuovi(c, "chiave") == "v" and not ha(c, "chiave"), "rimuovi chiave")
ok(chiavi([5, x: 1]) == [0, "x"], "chiavi")
ok(taglia([1, 2, 3, 4], 1, -1) == [2, 3], "taglia")
r = [3, 1, 2]
ok(ordina(r) == [1, 2, 3] and r == [3, 1, 2], "ordina non modifica")
ok(ordina(["bb", "a", "ccc"], |a, b| len(a) < len(b)) == ["a", "bb", "ccc"], "ordina con funzione")
copia_r = copia(r)
push(copia_r, 9)
ok(len(r) == 3, "copia")

# --- funzioni
fn quadrato(n) = n * n
ok(quadrato(5) == 25, "fn con =")
ok((|a, b| a + b)(2, 3) == 5, "lambda")
ok((fn(x) { x * 2 })(4) == 8, "fn anonima")
ok(([1, 2, 3] |> map(quadrato) |> sum) == 14, "pipe")
ok([1, 2, 3].map(quadrato).sum() == 14, "chiamata universale")
ok(2 `potenza` 10 == 1024, "infisso")
fn presto(n) { if n > 0 { return "positivo" }; "altro" }
ok(presto(1) == "positivo" and presto(-1) == "altro", "ritorna")
fn manca(a, b) = b
ok(manca(1) == nulla, "argomenti mancanti")

# --- valori predefiniti, argomenti variabili, spread
fn saluto(nome, parola = "ciao", fine = "!") = "{parola} {nome}{fine}"
ok(saluto("Ada") == "ciao Ada!" and saluto("Ada", "salve") == "salve Ada!", "valori predefiniti")
ok(saluto("Ada", nulla, "?") == "ciao Ada?", "nulla usa il predefinito")
fn tutti_arg(primo, ...resto) = [primo, resto]
ok(tutti_arg(1) == [1, []] and tutti_arg(1, 2, 3) == [1, [2, 3]], "...resto")
fn somma_tutti(...xs) = somma(xs)
ok(somma_tutti(...[1, 2], 3, ...[4]) == 10, "spread negli argomenti")
ok([0, ...[1, 2], ...[a: 1], 3] == [0, 1, 2, 3, a: 1], "spread nelle celle")
ok(massimo(...[3, 9, 2]) == 9, "spread con funzioni native")
f_def = |a, b = a * 2| a + b
ok(f_def(5) == 15 and f_def(5, 1) == 6, "predefinito che usa un parametro precedente")

# --- closure e ^
fn contatore() { n = 0; || { ^n += 1; n } }
k = contatore()
k(); k()
ok(k() == 3, "closure con ^")
g = 1
fn locale() { g = 99; g }
ok(locale() == 99 and g == 1, "assegnamento locale")

# --- oggetti e @
conto = [saldo: 0, deposita: |q| { @saldo += q; @ }]
conto.deposita(10).deposita(5)
ok(conto.saldo == 15, "metodi e @")
base = [saluta: || "ciao da {@nome}"]
ok((base + [nome: "Uni"]).saluta() == "ciao da Uni", "estensione con +")

# --- controllo del flusso
s = 0
per i in 1..10 { se i % 2 == 0 { continua }; se i > 7 { basta }; s += i }
ok(s == 1 + 3 + 5 + 7, "per / continua / basta")
ok(map(5..1, |x| x) == [5, 4, 3, 2, 1], "intervallo discendente")
ok(len(0..<10) == 10, "intervallo esclusivo")
t = []
per i in 3 { push(t, i) }
ok(t == [0, 1, 2], "per su numero")
fn gen() { i = 0; || if i < 3 { ^i += 1; i } }
ok(map(gen(), |x| x) == [1, 2, 3], "iteratore funzione")
x = se vero { "sì" } altrimenti { "no" }
ok(x == "sì", "se come espressione")
n = 0
mentre n < 5 { n += 1 }
ok(n == 5, "mentre")

# --- scegli
fn tipo_di(v) {
  scegli v {
    0 => "zero"
    1 | 2 => "poco"
    3..9 => "medio"
    [1, 2] => "coppia"
    "ciao" => "saluto"
    k se tipo(k) == "numero" and k < 0 => "negativo"
    _ => "altro"
  }
}
ok(map([0, 2, 5, -1, [1, 2], "ciao", 99], tipo_di) == ["zero", "poco", "medio", "negativo", "coppia", "saluto", "altro"], "scegli")

fn forma(v) = scegli v {
  [] => "vuota"
  [x, y] => x + y
  [nome: n] => n
  [0, _] | [_, 0] => "zero"
  _ => "altro"
}
ok(map([[], [2, 3], [nome: "Ada", anni: 1], [5], 7], forma) == ["vuota", 5, "Ada", "altro", "altro"], "destrutturazione")
ok(forma([0, 1, 2]) == "altro", "lunghezza esatta")

# --- errori
ok(prova(|| errore("x"), |e| "preso " + e) == "preso x", "prova / errore")
ok(prova(|| 1 + nulla) == nulla, "prova senza gestore")
ok(prova(|| errore([codice: 42]), |e| e.codice) == 42, "errore con valore")

# --- closure nei cicli: ogni giro ha la sua variabile
fs = []
per i in 0..<3 { push(fs, || i); ultimo_i = i }
ok(map(fs, |f| f()) == [0, 1, 2], "closure nei cicli")
ok(ultimo_i == 2, "variabili nuove del ciclo restano nella funzione")
fs = []
per k, v in [a: 1, b: 2] { push(fs, || k + v) }
ok(fs[0]() == "a1" and fs[1]() == "b2", "closure su chiave e valore")

# --- JSON
d = [nome: "Ada", anni: 36, tag: ["x", "y"], ok: vero, nota: nulla, t: "a\"b\né"]
ok(da_json(in_json(d)) == d, "JSON andata e ritorno")
ok(in_json([1, "a", [b: falso]]) == '[1,"a",{"b":false}]', "in_json")
ok(da_json('{"a": [1, 2.5, -3e2], "b": "\u00e8"}') == [a: [1, 2.5, -300], b: "è"], "da_json")
ok(in_json([]) == "[]" and in_json([1, x: 2]) == '{"0":1,"x":2}', "celle miste in JSON")
ok(prova(|| da_json("[1,"), |e| "errore") == "errore", "JSON non valido")
ok(lunghezza(dividi(in_json([a: 1], 2), "\n")) == 3, "JSON indentato")

# --- moduli
m = importa("modulo")
ok(m.saluta("Uni") == "ciao Uni", "importa relativo al file")
m.caricamenti += 1
ok(importa("modulo.uc").caricamenti == 2, "un modulo si carica una volta sola")

# --- varie
ok(esegui("6 * 7") == 42, "esegui")
ok(numero("3.5") == 3.5 and numero("x") == nulla, "numero")
ok(tipo(1) == "numero" and tipo(stampa) == "funzione", "tipo")
ok(type(1) == "number" and type("a") == "string" and type([]) == "cell" and type(nil) == "nil", "type")

# --- lingua
lingua_prima = lingua()
lingua("en")
ok(str(vero) == "true" and str(nulla) == "nil" and lingua() == "en", "stampa in inglese")
ok(prova(|| 1 + nulla, |e| e).has("invalid operation"), "errori in inglese")
lingua("it")
ok(str(vero) == "vero" and str([falso]) == "[falso]", "stampa in italiano")
ok(prova(|| 1 + nulla, |e| e).has("non valida"), "errori in italiano")
ok(tipo(1) == "numero" and type(1) == "number", "tipo e type non dipendono dalla lingua")
lingua(lingua_prima)
ok(carattere(codice("é")) == "é", "carattere/codice")
ok(volte(3, |i| i) == [0, 1, 2], "volte")
ok(accoppia([1, 2], ["a", "b"]) == [[1, "a"], [2, "b"]], "accoppia")
ok(tutti([2, 4], |x| x % 2 == 0) and qualcuno([1, 2], |x| x > 1), "tutti / qualcuno")

stampa("tutti i {superati} test superati ✓")
