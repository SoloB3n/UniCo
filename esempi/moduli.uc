# importa() esegue un file e restituisce le sue variabili come cella.
geo = importa("esempi/geometria")
stampa("cerchio:", arrotonda(geo.area_cerchio(2) * 100) / 100, geo.UNITA)
stampa("rettangolo:", geo.area_rettangolo(3, 4), geo.UNITA)

# Gli errori si gestiscono con prova(funzione, gestore).
fn dividi_sicuro(a, b) = prova(|| {
  se b == 0 { errore("divisione per zero") }
  a / b
}, |e| "impossibile: {e}")
stampa(dividi_sicuro(10, 2), "|", dividi_sicuro(1, 0))
