# Test del garbage collector generazionale: celle e ambienti vecchi che
# ricevono oggetti nuovi. Va eseguito anche con "make stress".
vecchia = []
diz = []
per i in 0..<30 { diz["k{i}"] = [i] }          # cella grande (con indice hash)
per giro in 0..<40 {
  push(vecchia, [n: giro, testo: "v{giro}"])   # lista vecchia, valori nuovi
  diz["k{giro % 30}"] = [giro, "x" * 3]        # sostituzione in mappa vecchia
  vecchia[0] = [n: 0, testo: "primo{giro}"]    # scrittura per indice
  se giro % 7 == 0 { rimuovi(diz, "k{giro % 30}"); diz["nuova{giro}"] = [giro] }
}
verifica(len(vecchia) == 40 and vecchia[39].testo == "v39", "lista vecchia")
verifica(vecchia[0].testo == "primo39", "scrittura per indice")
verifica(diz["k6"][1] == "xxx" and diz["k6"][0] == 36 and not ha(diz, "k5"), "mappa vecchia")

# closure: l'ambiente catturato invecchia e riceve valori nuovi
fn accumulatore() { tutti = []; |x| { push(tutti, [x, "{x}"]); tutti } }
acc = accumulatore()
per i in 0..<50 { acc(i) }
verifica(len(acc(0)) == 51 and acc(0)[49][1] == "49", "ambiente catturato")

# ^ su una variabile esterna di una funzione vecchia
fn contenitore() { val = nulla; [metti: |v| { ^val = v }, prendi: || val] }
c = contenitore()
per i in 0..<30 { c.metti([i, "n{i}"]) }
verifica(c.prendi()[1] == "n29", "^ con valori nuovi")

# ordinamento con comparatore che crea oggetti
xs = volte(60, |i| [v: (i * 37) % 60, s: "e{i}"])
ord = ordina(xs, |a, b| { tmp = [a.v, b.v]; tmp[0] < tmp[1] })
verifica(ord[0].v == 0 and ord[59].v == 59, "ordina con comparatore")

# JSON annidato e modulo importato che viene poi modificato
dati = da_json(in_json([lista: volte(20, |i| [i, [n: i]])]))
verifica(dati.lista[19][1].n == 19, "JSON")
m = importa("modulo")
per i in 0..<20 { m["extra{i}"] = [i] }
verifica(importa("modulo")["extra19"][0] == 19, "modulo modificato")
stampa("garbage collector: tutto a posto ✓")
