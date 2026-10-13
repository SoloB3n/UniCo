# La cella: un'unica struttura per liste, dizionari, oggetti e moduli.

lista = [10, 20, 30]
mappa = [nome: "Ada", anno: 1815]
mista = [1, 2, 3, colore: "blu"]       # entrambe le cose insieme

stampa(lista[0], lista[-1], mappa.nome, mappa["anno"], mista.colore)

# Un oggetto è una cella con funzioni dentro; "@" è la cella stessa.
punto = [
  x: 0, y: 0,
  sposta: |dx, dy| { @x += dx; @y += dy; @ },
  testo: || "({@x}, {@y})",
]
stampa(punto.sposta(3, 4).sposta(1, 1).testo())

# Niente classi: si estende una cella con "+".
animale = [verso: "...", parla: || "{@nome} dice {@verso}"]
cane  = animale + [nome: "Fido", verso: "bau"]
gatto = animale + [nome: "Micio", verso: "miao"]
per a in [cane, gatto] { stampa(a.parla()) }

# Iterare: valori, oppure coppie chiave/valore.
per k, v in mappa { stampa("  {k} -> {v}") }
