# Il gioco della vita di Conway, in UniCo.
L = 20; A = 10
griglia = volte(A, |_| volte(L, |_| caso() < 0.3))

fn vicini(g, r, c) {
  n = 0
  per dr in -1..1 {
    per dc in -1..1 {
      se (dr != 0 or dc != 0) and g[(r + dr) % A][(c + dc) % L] { n += 1 }
    }
  }
  n
}

fn passo(g) = volte(A, |r| volte(L, |c| {
  n = vicini(g, r, c)
  n == 3 or (g[r][c] and n == 2)
}))

per gen in 1..5 {
  stampa("generazione {gen}")
  per riga in griglia { stampa(riga.mappa(|v| se v { "█" } altrimenti { "·" }).unisci()) }
  griglia = passo(griglia)
}
