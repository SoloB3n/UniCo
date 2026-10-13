#!/bin/sh
# Test della riga di comando: limiti, modalità protetta, errori.
U=${1:-./unico}
fail=0
export UNICO_LANG=it   # i messaggi attesi qui sotto sono in italiano
check() {  # descrizione, output atteso (sottostringa), comando...
    desc=$1; want=$2; shift 2
    out=$("$@" 2>&1)
    case $out in
        *"$want"*) ;;
        *) echo "FALLITO: $desc"; echo "  atteso: $want"; echo "  ottenuto: $out"; fail=1 ;;
    esac
}
check "limite di passi"       "limite di passi"    $U --passi 1000 -e 'mentre vero { }'
check "limite di tempo"       "limite di tempo"    $U --tempo 0.2 -e 'n = 0; mentre vero { n += 1 }'
check "limite di memoria"     "limite di memoria"  $U --memoria 16 -e 'x = []; mentre vero { push(x, "abcdefgh" * 8) }'
check "limiti non intercettabili" "limite di passi" $U --passi 2000 -e 'mentre vero { prova(|| { mentre vero { } }, |e| 0) }'
check "protetto: file"        "modalità protetta"  $U --protetto -e 'read_file("x")'
check "protetto: importa"     "modalità protetta"  $U --sandbox -e 'import("x")'
check "protetto: esci"        "modalità protetta"  $U --sandbox -e 'exit(0)'
check "protetto: calcolo ok"  "42"                 $U --sandbox -e 'print(to_json([6 * 7]))'
check "catena di chiamate"    "in f, chiamata alla riga 2" $U -e 'fn f() = 1 + nil
f()'
check "graffa nel testo"      "usa \\{"            $U -e 'print("{")'
check "stdin e argomenti"     '["a", "b"]'         sh -c "echo 'print(ARGS)' | $U - a b"
check "messaggi in inglese"   "undefined name"     $U --lang en -e 'x'
check "inglese da ambiente"   "step limit"         env UNICO_LANG=en $U --steps 10 -e 'while true { }'
check "inglese: stampa"       "true nil"           $U --lang en -e 'print(vero, nulla)'
check "lingua sconosciuta"    "non disponibile"    $U --lingua xx -e '1'
check "lingua di sistema"     "nome non definito"  env -u UNICO_LANG LANG=it_IT.UTF-8 $U -e 'x'
if [ $fail = 0 ]; then echo "test della riga di comando superati ✓"; else exit 1; fi
