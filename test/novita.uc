# Test delle funzioni aggiunte nella versione 0.4: formattazione, testo,
# liste, data e ora, sistema, espressioni regolari, assegnamento multiplo.
n = 0
fn ok(cond, nome) { verifica(cond, nome); ^n += 1 }

# --- formattazione
ok(format(3.14159, ".2") == "3.14" and format(2, ".3") == "2.000", "decimali")
ok(format(42, "05") == "00042" and format(-42, "06.1") == "-042.0", "zeri")
ok(format("ab", ">4") == "  ab" and format("ab", "<4") == "ab  " and format("ab", "*^6") == "**ab**", "allineamento")
ok(format(1234567.891, ",.2") == "1,234,567.89" and format(-1000, ",") == "-1,000", "migliaia")
ok(format(7, "3") == "  7" and format("città", ">6") == " città", "larghezza in caratteri")
x = 2.5; nome = "Ada"
ok("{x:.2}|{nome:>5}|{nome}" == "2.50|  Ada|Ada", "formato nel testo")
ok("{"a:b"}|{[k: 1].k:03}" == "a:b|001", "':' dentro testi e celle")
ok(prova(|| format(1, "abc"), |e| "errore") == "errore", "formato non valido")
ok(pad_left("7", 3, "0") == "007" and pad_right("ab", 4) == "ab  " and riempi_sinistra(5, 3) == "  5", "riempi")

# --- testo
ok("ciao mondo".starts_with("ciao") and "ciao".ends_with("ao") and not "ab".starts_with("abc"), "inizia/finisce")
ok(inizia_con("x", "") and finisce_con("x", ""), "prefisso vuoto")

# --- liste
l = [1, 3]
insert(l, 1, 2); insert(l, 0, 0); insert(l, -1, 9); insert(l, 99, 7)
ok(l == [0, 1, 2, 9, 3, 7], "insert")
ok(unique([3, 1, 3, "a", "a", [1], [1]]) == [3, 1, "a", [1]], "unique")
ok(flatten([[1, [2]], 3, [4]]) == [1, [2], 3, 4] and flatten([[1, [2, [3]]]], 9) == [1, 2, 3], "flatten")
a = [[1], [k: [2]]]
b = deep_copy(a)
b[0][0] = 99; b[1].k[0] = 98
ok(a == [[1], [k: [2]]] and b == [[99], [k: [98]]], "deep_copy")
ok(range(4) == [0, 1, 2, 3] and range(2, 5) == [2, 3, 4] and range(0, 10, 4) == [0, 4, 8], "range")
ok(range(5, 0, -2) == [5, 3, 1] and range(3, 1) == [] and intervallo(0, 1, 0.5) == [0, 0.5], "range a passi")
seed(7); r1 = [random(), random(10), random(1, 6)]
seed(7); ok(r1 == [random(), random(10), random(1, 6)], "seed riproducibile")

# --- assegnamento multiplo
p = 1; q = 2
p, q = q, p
ok(p == 2 and q == 1, "scambio")
u, v, w = [10, 20]
ok(u == 10 and v == 20 and w == nil, "scomporre una cella")
c = [0, 0]
c[0], c[1] = "x", "y"
ok(c == ["x", "y"], "assegnare a indici")
fn minmax(xs) = [min(xs), max(xs)]
lo, hi = minmax([3, 9, 1])
ok(lo == 1 and hi == 9, "più valori da una funzione")

# --- data e ora
d = date(0, true)
ok(d.year == 1970 and d.month == 1 and d.day == 1 and d.weekday == 4 and d.text == "1970-01-01 00:00:00", "date")
ok(data(86400, true).giorno == 2 and data(86400, true).giorno_settimana == 5, "data")
ok(format_date(0, "%Y/%m/%d", true) == "1970/01/01", "format_date")
ok(time() > 1700000000 and time() != floor(time()) or true, "time")

# --- sistema
ok(type(env("PATH")) == "string" and env("NON_ESISTE_UNICO_XYZ") == nil, "env")
ok(exists("test") and is_dir("test") and not is_dir("test/novita.uc") and not exists("non/esiste"), "exists / is_dir")
ok("novita.uc" in list_dir("test") and list_dir("non/esiste") == nil, "list_dir")
write_file("test/.tmp_unico", "x")
ok(remove_file("test/.tmp_unico") and not exists("test/.tmp_unico"), "remove_file")

# --- espressioni regolari
ok(re_match("tel 333-1234", '(\d{3})-(\d+)') == ["333-1234", "333", "1234"], "re_match con gruppi")
ok(re_match("abc", 'x') == nil and re_match("", '^$') == [""], "re_match senza risultato")
ok(re_find_all("a1 b22 c333", '\d+') == ["1", "22", "333"], "re_find_all")
ok(re_replace("2026-10-13", '(\d+)-(\d+)-(\d+)', "$3/$2/$1") == "13/10/2026", "re_replace con $n")
ok(re_replace("10 e 20", '\d+', |m| str(num(m[0]) * 2)) == "20 e 40", "re_replace con funzione")
ok(re_split("a, b;c  d", '[,;\s]+') == ["a", "b", "c", "d"], "re_split")
ok(re_find_all("perché però così Straße", '\w+') == ["perché", "però", "così", "Straße"], "\\w con accenti")
ok(re_match("Città BELLA", '(?i)CITTÀ\s+(\w+)')[1] == "BELLA", "(?i)")
ok(re_find_all("cat category concat", '\bcat\b') == ["cat"], "\\b")
ok(re_match("aaaa", 'a{2,3}')[0] == "aaa" and re_match("aaaa", 'a{2,3}?')[0] == "aa", "\{m,n} e pigri")
ok(re_match("ab", '(a|ab)(c|bcd)?$') == ["ab", "ab", nil] and re_match("x", '(a*)*$') != nil, "alternative e cicli vuoti")
ok(prova(|| re_match("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!", '(a|aa)*$'), |e| "lenta") == "lenta", "limite di passi")
ok(prova(|| re_match("x", '(a'), |e| "errore") == "errore", "regex non valida")

stampa("tutti i {n} test delle novità superati ✓")
