## Oppgave 1-2: Analyse av algoritmen

Programmet finner den beste perioden for ett kjøp og ett salg ved å finne den maksimale deltabellen i arrayet med kursendringer. Hver verdi representerer hvor mye aksjekursen endrer seg fra én dag til den neste.

Funksjonen `maxSubarray` i `algoritme.cpp` gjennom kursendringene én gang og holder oversikt over følgende:

- `currentSubarraySum`: fortjenesten til den pågående perioden
- `maxSubarraySum`: den største fortjenesten som er funnet
- `currentStartIndex`: startindeksen til den pågående perioden
- `maxSubarray`: start- og sluttindeksen til den beste perioden

For hver kursendring legges verdien til den pågående summen. Hvis summen blir negativ, forkastes perioden, fordi en negativ foreløpig fortjeneste aldri vil gjøre en senere periode bedre. Algoritmen starter da en ny periode på neste indeks. Dersom den pågående summen blir større enn den lagrede maksimalen, vil det nåværende indekset lagres som nytt sluttpunkt.

### Tidskompleksitet

Løkken i `maxSubarray` går gjennom tabellen én gang. Før løkken kjøres, evaluerer algoritmen en kondisjon og deklarerer 4 variabler; altså: $c_1$. Så, for hvert element `n` utføres det én addisjon, to kondisjoner, og eventuelle tilordninger; altså: $c_2n$.

Dersom tabellen har `n` elementer, blir kjøretiden $T(n)$ derfor:

$$
T(n) = c_1 + c_2n
$$

Konstantene har liten betydning for store `n`, så tidskompleksiteten blir:

$$
\boxed{O(n)}
$$

Dette betyr at dersom problemstørrelsen dobles, forventes omtrent en dobling av kjøretiden.

### Plasskompleksitet

Selve `maxSubarray` bruker bare et konstant antall variabler, $4$, som deklareres før for-løkken. Algoritmen bruker derfor, uavhengig av størrelsen på inputtabellen:

$$
\boxed{O(1)}
$$

ekstra minne.

Merk at programmet lagrer riktignok alle kursendringene i en `std::vector`. Selve datasettet krever derfor $O(n)$ minne, men dette er inputlagring og ikke ekstra minne brukt av algoritmen.

Funksjonen `generateData` i `algoritme.cpp` fyller tabellen med `n` tilfeldige verdier og har også tidskompleksitet $O(n)$. Denne funksjonen kjøres før tidsmålingen, slik at målingene hovedsakelig viser kjøretiden til `maxSubarray`.

### Forventede tidsmålinger

Siden algoritmen er lineær, bør målingene omtrent følge dette mønsteret:

| Problemstørrelse | Forventet kjøretid |
|---:|---:|
| 10 000 000 | $t$ |
| 20 000 000 | omtrent $2t$ |
| 40 000 000 | omtrent $4t$ |
| 80 000 000 | omtrent $8t$ |

Målingene vil ikke nødvendigvis bli helt nøyaktige på grunn av prosessbelastning, minnehastighet, og unøyaktigheter i systemklokken. Likevel bør kjøretiden øke omtrent proporsjonalt med `n`. Dersom dette observeres i `oppgave 1-3.cpp`, bekrefter det den teoretiske analysen om at algoritmen har tidskompleksitet $O(n)$.

### Håndtering av negative kursendringer

Dersom alle kursendringene er negative, finner algoritmen den
kursendringen som gir minst tap. For eksempel vil `[-5, -2, -8]`
gi `-2` som beste resultat. Dette er riktig når ett kjøp og ett salg
må gjennomføres.