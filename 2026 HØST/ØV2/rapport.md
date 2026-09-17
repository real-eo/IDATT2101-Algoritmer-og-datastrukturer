# Tidsmålingene og analyse av rekursive eksponentmetoder

Koden i `eksponenter.cpp` inneholder tre ulike måter å beregne $x^n$:

- Metode 1: rekursiv linær reduksjon
- Metode 2: rekursiv halvering
- Metode 3: standardbiblioteket `std::pow` for sammenligning

Alle metodene testes med en kjent verdi:

$$
5^{11} = 48828125
$$

Dette er også verifisert i programmet med `assert`, så korrektheten er kontrollert før tidsmålingene.

### Tidsmålinger

| Size (n) | Metode 1 (ns) | Metode 2 (ns) | Metode 3 (ns) |
|---:|---:|---:|---:|
| 1000 | 7856.23 | 22.1428 | 11.6657 |
| 2000 | 17757.3 | 23.4909 | 12.0007 |
| 4000 | 33162.5 | 25.7884 | 11.7175 |
| 8000 | 57763 | 27.667 | 11.7992 |
| 16000 | 100256 | 29.8093 | 11.7751 |
| 32000 | 272760 | 33.2771 | 12.1599 |

Utifra dette, ser det tydelig ut som at:

- Metode 1 vokser raskt med $n$
- Metode 2 er nesten konstant i forhold til $n$
- Metode 3 er også nesten konstant og fungerer som referanse


## Asymptotisk analyse

### Metode 1
Antall rekursive kall er nesten $n$.

Hvert kall gjør konstant arbeid:

- sammenlikning av eksponenten
- en multiplikasjon
- ett rekursivt kall

Dermed blir rekursionstreet:

$$
T_1(n) = T_1(n-1) + \Theta(1)
$$

Løser dette til:

$$
T_1(n) = \Theta(n)
$$

Dette stemmer med målingene: når $n$ dobles, øker kjøretiden omtrent lineært. For eksempel:

- $1000 \rightarrow 7856$ ns
- $2000 \rightarrow 17757$ ns
- $4000 \rightarrow 33162$ ns
- $8000 \rightarrow 57763$ ns

Dette er typisk for en linær algoritme.

<!-- --- -->

### Metode 2
Her halveres eksponenten i hvert rekursivt kall:

$$
T_2(n) = T_2\left(\frac{n}{2}\right) + \Theta(1)
$$

Dette er standardformen for rekursjon med halvering, og løser til:

$$
T_2(n) = \Theta(\log n)
$$

Det er nettopp dette som forklarer de svært lave målingene. Eksponenten blir raskt redusert, så antall kall er omtrent $\log_2 n$, ikke $n$.

Eksempel:

- $n=1000$ gir omtrent 10 nivåer
- $n=32000$ gir omtrent 15 nivåer

Det er derfor helt naturlig at tiden forblir nesten konstant rundt 20–35 ns.


## Konklusjon

**Hvorfor ser målingene så forskjellige ut?**

Forskjellen kommer fra hvor raskt problemet blir mindre.

- Metode 1: eksponenten går fra $n$ til $n-1$, så rekursjonen må gjøre $n$ steg.
- Metode 2: eksponenten går fra $n$ til $n/2$, så rekursjonen må gjøre bare $\log_2 n$ steg.

De to rekursive metodene beregner samme ting, men på grunn av ulik asymptotisk kompleksitet, ser målingene svært forskjelle ut:

- Metode 1: $ \Theta(n) $
- Metode 2: $ \Theta(\log n) $

For store verdier av $n$ vil metode 1 bli betydelig tregere sammenliknet med metode 2 - som forblir rask.

Dette stemmer godt overens med målingene. For store $n$ viser metode 1 tydelig lineær vekst, mens metode 2 er nærmest konstant. I tillegg, ser vi at metode 2 har en omtrent konstant differenase mellom hver måling til tross for at $n$ dobbles. Dette betyr at metode 2 er en bedre algoritme for større verdier av $n4 

