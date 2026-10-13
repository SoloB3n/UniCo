# Lo stesso programma, scritto in italiano e in inglese.
# Le parole chiave si possono anche mescolare liberamente.

per i in 1..15 {
  stampa(scegli [i % 3, i % 5] {
    [0, 0] => "FizzBuzz"
    [0, _] => "Fizz"
    [_, 0] => "Buzz"
    _ => i
  })
}

for i in 1..15 {
  if i % 15 == 0 { print("FizzBuzz") }
  else if i % 3 == 0 { print("Fizz") }
  else if i % 5 == 0 { print("Buzz") }
  else { print(i) }
}
