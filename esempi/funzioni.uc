# Tre modi di chiamare la stessa funzione.
numeri = [5, 3, 8, 1, 9, 2]

a = somma(mappa(filtra(numeri, |n| n > 2), |n| n * n))   # classico
b = numeri.filtra(|n| n > 2).mappa(|n| n * n).somma()    # a catena
c = numeri                                               # a tubo
  |> filtra(|n| n > 2)
  |> mappa(|n| n * n)
  |> somma
stampa(a, b, c)

# Qualunque funzione a due argomenti diventa un operatore.
fn media(x, y) = (x + y) / 2
stampa(10 `media` 20, 2 `potenza` 8, 3 `massimo` 7)

# Closure: "^" modifica una variabile della funzione esterna.
fn contatore(passo) {
  n = 0
  || { ^n += passo; n }
}
c = contatore(10)
c(); c()
stampa("contatore:", c())

# Un generatore è una funzione che restituisce nulla quando ha finito.
fn fibonacci(limite) {
  a = 0; b = 1
  || {
    se a > limite { ritorna nulla }
    v = a
    ^a = b; ^b = v + b
    v
  }
}
stampa(mappa(fibonacci(100), |x| x))
