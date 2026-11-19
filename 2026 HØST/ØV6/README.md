# Øving 6 - Datakompresjon

Komprimering og dekomprimering av filer med kontekstmodellering og aritmetisk koding (context mixing), skrevet i C++.

Løsningen er inspirert av PAQ-familien av komprimering (Matt Mahoney), som bruker kontekstmodellering med blanding (context mixing). Teknikkene som brukes **_(adaptive tellere, bit-historikk med StateMap, logistisk mikser, SSE/APM og binær aritmetisk koding)_** bygger på beskrivelsen av algoritmene, ikke på eksisterende kildekode. Kontekstene er valgt for testfilene, blant annet en egen tekststrøm og linjemodeller for LyX-formatet.

Komprimering og utpakking er to separate programmer. Utpakking trenger kun den komprimerte fila.

## Filer
### Programmer

| Fil                          | Beskrivelse                          |
| ---------------------------- | ------------------------------------ |
| `compress.cpp`               | Komprimerer en fil                   |
| `decompress.cpp`             | Pakker ut en komprimert fil          |

### `header.h`

Alle modulene ligger i én header, sortert i avhengighetsrekkefølge.

| Seksjon                      | Beskrivelse                          |
| ---------------------------- | ------------------------------------ |
| `parameters` / `hash32`      | Konstanter, hashing og filbehandling |
| `AdaptiveCounter`            | Adaptiv bitteller                    |
| `squash` / `stretch`         | Logistiske hjelpefunksjoner i fastpunkt |
| `Mixer`                      | Logistisk mikser i to lag            |
| `AdaptiveProbabilityMap`     | SSE/APM                              |
| `ArithmeticEncoder` / `ArithmeticDecoder` | Binær aritmetisk koder og dekoder |
| `BitHistory` / `StateMap`    | Bit-historikk og `StateMap`          |
| `MatchModel`                 | Lempel-Ziv-lignende matchmodell      |
| `Predictor`                  | Kontekstmodellene og hele modellen   |

## Systemkrav

Kontekstmodellene bruker hashtabeller med opptil 2²² plasser hver, og det er
32 kontekstmodeller. For filer over noen hundre kB (der tabellene når
maksimal størrelse) bruker programmet rundt 1 GB RAM, uavhengig av hvor mye
større filen er utover det.

## Kompilering

Krever en C++23-kompilator (GCC, Clang eller MSVC).

**Linux / Mac:**

```bash
g++ -O3 -march=native -std=c++23 -o compress compress.cpp
g++ -O3 -march=native -std=c++23 -o decompress decompress.cpp
```

**Windows (MSVC, kjøres i «Developer Command Prompt for VS»):**

```cmd
cl /O2 /std:c++latest /EHsc compress.cpp
cl /O2 /std:c++latest /EHsc decompress.cpp
```

`/std:c++latest` gir C++23 på VS 2022. Fra MSVC 14.52 (VS 2026) kan
`/std:c++23` brukes i stedet.

**Windows (MinGW / g++):**

```cmd
g++ -O3 -march=native -std=c++23 -o compress.exe compress.cpp
g++ -O3 -march=native -std=c++23 -o decompress.exe decompress.cpp
```

`-O3 -march=native` gir rundt 20 % raskere kjøring og gir bit-identisk utdata,
siden modellen er ren heltallsaritmetikk. Fjern `-march=native` hvis binæren skal
kjøres på en annen maskin enn den ble bygget på.

## Kjøring

Programmet tar filnavn som argumenter, så filene trenger ikke ligge i samme
mappe som programmene.

`compress` legger `.kmp` til filnavnet, og `decompress` fjerner den igjen:

**Linux / Mac:**

```bash
./compress diverse.lyx          # lager diverse.lyx.kmp
./decompress diverse.lyx.kmp    # lager diverse.lyx, eller "diverse (1).lyx" hvis den finnes
```

**Windows:**

```cmd
compress.exe diverse.lyx
decompress.exe diverse.lyx.kmp
```

`decompress` overskriver aldri en fil som finnes fra før, men legger til
` (1)`, ` (2)` osv. foran filendelsen. Utfilnavnet kan også oppgis selv:
`./compress diverse.lyx x.kmp` og `./decompress x.kmp ut.lyx`.

## Verifisering

Utpakket fil skal være byte-for-byte lik originalen.

**Linux / Mac:**

```bash
diff diverse.lyx "diverse (1).lyx" && echo "Identiske"
```

**Windows:**

```cmd
fc /b diverse.lyx "diverse (1).lyx"
```

> Bruk `/b` (binær sammenligning). Uten den sammenligner `fc` som tekst og kan overse forskjeller i linjeskift.

## Resultater

| Testfil                                     | Original (bytes) | Komprimert (bytes) |   Andel |
| ------------------------------------------- | ---------------: | -----------------: | ------: |
| `diverse.lyx`                               |          184 300 |              6 568 |  3,56 % |
| `diverse.txt`                               |           16 973 |              4 118 | 24,26 % |
| `opg6-kompr.lyx`                            |           34 751 |              6 937 | 19,96 % |
| `Twenty_thousand_leagues_under_the_sea.txt` |          609 116 |            140 796 | 23,11 % |
| `enwik8`                                    |      100 000 000 |         18 584 213 | 18,58 % |

## Algoritme

Filen kodes én bit om gangen. For hver bit lager modellen en sannsynlighet for at
biten er 1, og en **aritmetisk koder** bruker den. Jo bedre sannsynligheten
treffer, jo færre bits trengs. Dekompressoren kjører nøyaktig samme modell og får
dermed de samme sannsynlighetene, så ingen tabeller må lagres.

**Kontekstmodeller (32 stk.)** ser på det som kom foran og slår opp hva som
pleier å komme etter:

- ordenmodeller: de siste 1, 2, 3, 4, 5, 6, 8, 12, 16 og 32 bytene
- ordmodeller: nåværende ord, forrige ord, ordet før det, og ordparene
- linje- og kolonnemodeller: tegnet i samme kolonne på forrige linje,
  starten av linja og starten av de to forrige linjene
- tekststrøm: kontekster bygget bare av tekstlinjer, slik at LyX-markup mellom
  avsnittene ikke ødelegger konteksten for teksten
- indirekte kontekst: hva som fulgte de to siste bytene forrige gang
- spredte kontekster: `c1+c3`, `c2+c4`, `c1+c4`, `c1+c3+c5` — hopper over
  nabobytene og fanger opp mønstre som ikke er sammenhengende

Hver kontekstmodell gir tre prediksjoner: en adaptiv teller, en
**bit-historikk** (hvor mange 0-er og 1-ere som er sett nylig, oversatt til
sannsynlighet av en lært tabell) og en **byte-prediksjon** (byten som kom sist
i denne konteksten, og hvor mange ganger på rad).

Hver kontekstmodell har sin egen hashtabell med `tableSize_` plasser (satt
etter filstørrelsen, opptil 2²²). For å unngå at to ulike kontekster som
hasher til samme plass skal dele og forstyrre hverandres lærte tilstand,
lagres en 8-bits eierkontroll per plass som del av en liten "bucket" på to
plasser (`slot` og `slot ^ 1`). Stemmer ikke kontrollen for `slot`, brukes
`slot ^ 1` i stedet hvis den stemmer der; stemmer den ikke på noen av dem,
nullstilles den minst trente av de to plassene og den nye konteksten tar
over. Telleren, tilstandsbyten og kontrollbyten ligger sammen i én 8-byte
`Slot`, slik at et oppslag er ett minneaksess i stedet for to.

**Matchmodellen** finner forrige forekomst av de siste 6 bytene og spår at det
som kom etter da, kommer igjen. Det er Lempel-Ziv-ideen (LZ) i en form som passer
med resten av modellen.

**Mikseren** er et lite nevralt nett i to lag som kombinerer alle
prediksjonene. Vektene læres underveis, og ulike vektsett velges etter
kontekst (bl.a. lengden på matchen), så nettet lærer når det skal stole på
hvilken modell. Lag 1 har sju utganger med hvert sitt vektsett:
`(matchtilstand, c0)`, `c1`, `(bitnummer, c2)`, `(antall sette ordener,
matchtilstand)`, `c0`, `(c1, c0)` og en hashet `(c2, c0)`.

**SSE/APM**: tre trinn som etterjusterer den endelige sannsynligheten.

All aritmetikk er heltall, slik at koder og dekoder gir identiske resultater
uavhengig av kompilator og plattform.

**Filformat:** filstørrelsen som varint (7 bits per byte), deretter utdata fra
den aritmetiske koderen.

## Ytelse

Modellen kodes én bit om gangen, så det er de indre løkkene som avgjør
hastigheten. Disse grepene gir rundt 20 % raskere kjøring uten å endre en
eneste bit i utdata:

- **Tabellene bygges ved kompilering.** `squash`, `stretch` og
  resiproktabellen til telleren var funksjonslokale `static`-tabeller. De ble
  riktignok bare initialisert én gang, men hvert kall måtte likevel sjekke en
  trådsikker vakt. `stretch` kalles rundt hundre ganger per bit, så vakten ble
  dyr. Nå er tabellene `constexpr` og bygges ferdig av kompilatoren.
- **Mikseren er en mal.** `Mixer<InputCount>` gjør inputantallet til en
  kompileringstidskonstant, slik at prikkproduktet og vektoppdateringen kan
  rulles ut. Med et kjøretidsantall måtte kompilatoren beholde løkkene.
- **Kontekstløkkene er kompileringstidsutrullet.** `compute()` bruker en
  fold-uttrykk over `std::index_sequence`, så tabellbase, tellergrense og
  `ORDER_0`-spesialtilfellet blir konstanter i stedet for forgreninger.
- **Én cache-linje per kontekst.** Telleren (4 B), bit-historikken (1 B) og
  eierskapssjekken (1 B) lå før i to separate tabeller, så hvert oppslag traff
  to cache-linjer. Nå ligger de i én 8-byte `Slot`, så oppslaget er ett
  minneaksess i stedet for to.
- **Oppslagene løses i to faser.** `compute()` kaller først `resolveSlot<I>()`
  for alle 32 kontekstene, deretter `contextInput<I>()`. Hvert oppslag er en
  DRAM-miss på flere hundre sykluser; når de avhengige beregningene lå mellom
  oppslagene, ble missene seriekjørt. Nå utstedes alle 32 oppslagene etter
  hverandre, så minne-nivå-parallellismen kan overlappe dem.
- **Tabellene er flate.** `slots_` og `runTables_` var 32 separate
  `std::vector`-allokeringer hver. Nå er de én sammenhengende blokk per tabell,
  så den indre løkken slipper pekerjaging per bit.
- **Gjentatte hasher er bufret.** `hash32` av de to siste bytene ble regnet ut
  to ganger per bit; nå én gang per byte. `contextHash_ & tableMask_` er
  forhåndsregnet i `startByte()`.
- **Store sider for slot-tabellen.** Tabellen er flere hundre megabyte og
  aksesseres tilfeldig, så den bommer på TLB i tillegg til cachen. Nå
  allokeres den 2 MiB-justert med `_aligned_malloc` / `aligned_alloc`, som
  reduserer antall TLB-oppføringer med 512×.
- **`-O3 -march=native`.** Modellen er ren heltallsaritmetikk, så
  vektorisering og bedre instruksjonsvalg endrer ikke resultatet.

Målt med `-O3 -march=native` på `Twenty_thousand_leagues_under_the_sea.txt`
(609 kB): 15,8 s → 13,1 s, altså **1,21×**. De øvrige testfilene gir 1,15–1,19×.
Utdata er byte-for-byte identisk med før optimaliseringen, verifisert med
SHA-256 på alle testfilene.

Ting som ble prøvd og forkastet fordi de ikke ga noe: `-flto`, PGO,
`__builtin_prefetch` (både i `resolveSlot` og for neste bit), å slå sammen alle
`StateMap`-tabellene til én blokk, og å bunte de 32 `hash32`-kallene i
`startByte()`.

**Tråding er målt og forkastet.** De 32 kontekstene er uavhengige og kunne
kjøres parallelt, og maskinen skalerer godt: fire prosesser samtidig bruker bare
1,25× tiden av én, altså 3,2× gjennomstrømning. Problemet er at mikseren må ha
alle 99 inputene i fast rekkefølge, så hver bit krever en synkronisering.
Målt med en spin-barrier på 22 kjerner (annen maskin og bygg enn målingene
over, derfor lavere enkelttrådstall):

| Tråder | Tid |
| -----: | --: |
| 1 | 10,6 s |
| 2 | 12,5 s |
| 4 | 16,0 s |
| 8 | 19,5 s |

Barrieren koster mer enn den halveringen av arbeidet den gir, fordi hver bit
bare er rundt 1000 sykluser. Tråding ligger derfor ikke i den endelige koden.
