//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC
#include <chrono>
#include <iomanip>


int main() {
    // 1. Verifikasjonstest (Liten n for å sjekke logikk)
    std::vector<signed char> testData;
    generateData(testData, 10);
    
    std::cout << "--- VERIFIKASJONSTEST ---\nData: ";
    for (signed char c : testData) std::cout << static_cast<int>(c) << " ";
    std::cout << "\n";
    
    Subarray testResult = oppgave1(testData);
    std::cout << "Beste kjop (start-indeks): " << testResult.start << "\n";
    std::cout << "Beste salg (slutt-indeks): " << testResult.end << "\n\n";

    // 2. YTELSESTEST / TIDSMÅLING (Flere ulike n)
    std::cout << "--- YTELSESTEST (Tidsmaling) ---\n";
    std::cout << std::left << std::setw(15) << "Problemstr. (n)" << "Kjoretid (ms)\n";
    std::cout << "---------------------------------\n";

    // Vi tester n = 10M, 20M, 40M, 80M for å se skaleringen tydelig
    std::vector<std::size_t> storrelser = {10'000'000, 20'000'000, 40'000'000, 80'000'000};
    std::vector<signed char> kursforandringer;

    for (std::size_t n : storrelser) {
        // Generer data UTENFOR tidsmålingen (siden math.random/RNG er tregt)
        generateData(kursforandringer, n);

        // Start klokken
        auto startKlokke = std::chrono::high_resolution_clock::now();

        // Kjor algoritmen (lagre resultatet i en variabel så kompilatoren ikke sletter koden)
        volatile Subarray result = oppgave1(kursforandringer);

        // Stopp klokken
        auto stoppKlokke = std::chrono::high_resolution_clock::now();
        
        // Beregn varighet i millisekunder
        auto varighet = std::chrono::duration_cast<std::chrono::milliseconds>(stoppKlokke - startKlokke).count();

        std::cout << std::left << std::setw(15) << n << varighet << " ms\n";
    }

    return 0;
}
