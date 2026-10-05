# Tidsmålinger og analyse av Shellsort med ulike gap-sekvenser

Koden i `alternativ 2.cpp` implementerer Shellsort med et konfigurerbart delingstall for gap-sekvensen. Gapet starter på $n/2$ og deles med delingstallet for hver passering, inntil det når 1 (og deretter 0, som avslutter sorteringen):

$$
s = \begin{cases} 0 & \text{hvis } s = 1 \\ 1 & \text{hvis } s = 2 \\ \max(1, \lfloor s / d \rfloor) & \text{ellers} \end{cases}
$$

der $d$ er delingstallet. 

Spesialtilfellene $s = 1 \rightarrow 0$ og $s = 2 \rightarrow 1$ er nødvendige: uten dem ville enten løkka aldri avsluttet (clampen gjør at $s$ aldri går under 1), eller sekvensen ville hoppet over gap 1 for store $d$ (f.eks. $3/3 = 0$), slik at tabellen ikke ble sortert.

Korrektheten er verifisert i programmet med to tester etter hver sortering:

- **Checksum-test**: summen av alle elementer sammenlignes før og etter sortering
- **Sekvens-test:** `std::transform_reduce` sjekker at $t[i] \le t[i+1]$ for alle $i$

Alle målingene nedenfor er på **50 millioner tilfeldige tall** i området $[0, 2^{31}-1]$, og alle divisorene ga `OK` på begge tester.

## Del 1: Valg av delingstall

### Tidsmålinger

| Divisor | Tid (ms) |
|---:|---:|
| 1.3 | 6808.2 |
| 1.5 | 6394.0 |
| 1.6 | 6411.1 |
| **1.7** | **6392.8** |
| 1.8 | 6575.6 |
| 1.9 | 6610.7 |
| 2.0 | 9189.4 |
| 2.1 | 6796.1 |
| 2.2 | 6781.4 |
| 2.5 | 7150.5 |
| 3.0 | 8441.6 |

### Observasjoner

Resultatene viser akkurat det deloppgaven forutsa, altså at både for små og for store delingstall gir dårligere resultat.

- **For nær 1** ($1.3$): gapet minker sakte, så det blir mange gap-verdier og dermed mange passeringer over tabellen. Hver passering har overhead, og totaltiden øker.
- **For stort** ($3.0$): gapet minker raskt, så det blir få passeringer. Da blir de siste gap-verdiene store, og passeringen med gap 1 minner om ren innsettingssortering over nesten-tilfeldige data - som har kvadratisk kjøretid.
- **Spesialtilfellet $2.0$**: divisor $2.0$ er markant tregere ($9189$ ms) enn naboene. Dette er en kjent svakhet ved Shells *originale* sekvens ($n/2, n/4, n/8, \dots$): alle gap-verdiene deler felles faktorer, slik at elementer på partalls- og oddetallsplasser i praksis ikke blandes før gap 1. Den siste passeringen får da uforholdsmessig mye arbeid. Divisor $2.0$ er altså et patologisk tilfelle, ikke et representativt "stort" tall.

### Konklusjon del 1

Det beste delingstallet i dette eksperimentet er **$1.7$** ($6392.8$ ms), tett fulgt av $1.5$ og $1.6$. Sweet spot-et ligger rundt **$1.5$–$1.7$** - lavere enn lærebokas $2.2$, som i vårt eksperiment gir ~$6781$ ms. Balansen går på å ha nok gap-verdier til å blande elementene grundig, men ikke så mange at passeringsoverheaden dominerer.

## Del 2: Måling av kompleksitet

For den beste varianten, $1.7$, måles kjøretiden på datasett av doblende størrelser, og kompleksiteten antas å være på formen $O(n^x)$.

### Metode

Antakelsen $T(n) = c \cdot n^x$ lineariseres ved å ta logaritmen på begge sider:

$$
\ln T(n) = \ln c + x \cdot \ln n
$$

I log-log-rommet er dette en rett linje med stigningstall $x$. Eksponenten finnes på to måter:

1. Parvis estimat mellom to målinger der $n$ dobles:

$$
x = \log_2\left(\frac{T_2}{T_1}\right)
$$

2. Minste kvadraters tilpasning av en linje gjennom alle $(\ln n, \ln T)$-punktene, som gir ett samlet estimat som bruker alle målingene.

### Tidsmålinger

| Size (n) | Tid (ms) | x est. |
|---:|---:|---:|
| 1 000 000 | 95.6 | — |
| 2 000 000 | 198.1 | 1.051 |
| 4 000 000 | 424.8 | 1.101 |
| 8 000 000 | 897.4 | 1.079 |
| 16 000 000 | 1912.7 | 1.092 |
| 32 000 000 | 3974.2 | 1.055 |

**Estimert kompleksitet: $O(n^{1.08})$**

### Observasjoner

- De parvise estimatene er svært stabile ($1.05$–$1.10$) på tvers av alle størrelser, noe som tyder på at målingene faktisk følger en potensfunksjon godt.
- Når $n$ dobles, øker kjøretiden med en faktor på omtrent $2^{1.08} \approx 2.11$ - altså litt mer enn dobling, men langt mindre enn firedobling - som ville tilsvart $x = 2$.
- Eksponenten ligger nær 1, men tydelig over. Dette stemmer med teorien: Shellsort med gode gap-sekvenser har verste case rundt $O(n^{3/2})$ eller bedre avhengig av sekvensen, mens *gjennomsnittlig* oppførsel på tilfeldige data i praksis ligger betydelig lavere. Vårt mål på $x \approx 1.08$ er konsistent med at Shellsort med en god gap-sekvens oppfører seg nesten lineært på tilfeldige data i dette størrelsesområdet.
- En mulig kilde til avvik er cache-effekter: ved store $n$ passer ikke dataene i cache, og strided minneaksess ved store gap er cache-fiendtlig. At estimatet likevel holder seg stabilt tyder på at denne effekten er jevn over størrelsene vi målte.

## Samlet konklusjon

- Det beste delingstallet for $50$ millioner tall i dette eksperimentet er **$1.7$**, med sweet spot-området $1.5$–$1.7$. Både mindre og større delingstall gir dårligere resultat, som forventet.
- Divisor $2.0$ er et spesielt dårlig valg på grunn av felles faktorer i gap-sekvensen - et funn som viser at *hvordan* gapene reduseres betyr mer enn bare antallet.
- Kompleksiteten til den beste varianten måles til **$O(n^{1.08})$** på tilfeldige data, med svært stabile parvise estimater. Shellsort med en godt valgt gap-sekvens oppfører seg dermed nesten lineært i praksis på dette datasettet - langt fra den kvadratiske kjøretiden til ren innsettingssortering som algoritmen bygger på.