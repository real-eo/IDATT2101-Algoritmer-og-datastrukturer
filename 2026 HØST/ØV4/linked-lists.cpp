//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC
#include <algorithm>
#include <iostream>
#include <memory>
#include <ranges>
#include <string>


// * Definitions
struct Node {
    std::unique_ptr<Node> next;                                                         // ? Reduce memory usage by declaring the bigger data types first
    Node* previous;                                                                     // ! USE RAW POINTERS FOR PREVIOUS POINTERS! All types of smart pointers    
    signed char digit;                                                                  // ! would be incorrect here. A shared pointer would cause unnecessary 
                                                                                        // ! overhead, along with impling ownership, like a unique pointer, which
    ~Node() {                                                                           // ! is wrong, and a weak pointer requires a shared pointer to be valid. A 
        // Iterative teardown: detach the chain before deleting, so                     // ! reference is also wrong as it can't be null, which we need for the head.
        // ~Node never recurses. O(n) time, O(1) auxiliary space.
        Node* current = next.release();

        while (current) {
            Node* next = current->next.release();                                       // detach first, so no recursion
            delete current;                                                             // ~Node runs, sees null next, returns
            
            current = next;
        }
    }

    // ? Note:
    // ?    I've omitted a ctor for Node to use aggregate initialization. The
    // ?    downside to this is that std::make_unique<Node>() can't be used, 
    // ?    so I instead have to let a unique_ptr<Node> take ownership of a raw 
    // ?    pointer, which is a bit more verbose, but it allows for more flexibility
};


class BigInt {
private:
    std::unique_ptr<Node> head;                                                         // Head is the most significant digit, and the tail is the least significant digit
    Node* tail = nullptr;                                                               // Tail is a raw pointer because it doesn't own the memory, the head does
    bool isNegative;

    //  * Ctor 
    // Adopt an already-built list, used by the arithmetic ops
    BigInt(std::unique_ptr<Node> list, bool negative) : 
        head(std::move(list)), 
        isNegative(negative) 
    {
        // Set the tail pointer to the last node in the list
        for (Node* p = head.get(); p; p = p->next.get())  {
            tail = p;
        }
    }


    // * Helpers
    //                      The sign logic
    // [  A  |  B  |                    RESULT                    ] 
    // [  −  |  −  | add magnitudes, sign −                       ]
    // [  +  |  +  | add magnitudes, sign +                       ]
    // [  +  |  −  | subtract smaller from larger, sign of larger ]
    // [  −  |  +  | subtract smaller from larger, sign of larger ]

    enum class ComparisonResult { LESS = -1, EQUAL = 0, GREATER = 1 };                  // ? Use enum class to avoid polluting the global namespace with macros or constants
    
    // Prepend a digit, used for LSD-first result construction.
    // ? Prepending makes each new digit the head, so the finished list is MSD-first with O(1) work per digit.
    void prependDigit(signed char d) {
        head = std::unique_ptr<Node>(new Node{
            .next = std::move(head),
            .previous = nullptr,
            .digit = d
        });

        // Update the previous pointer of the new head's next node, if it exists
        if (head->next) head->next->previous = head.get();
        else            tail = head.get();
    }


    // Add magnitudes, |a| + |b|, digit by digit from the LSD ends
    static BigInt addMagnitudes(const BigInt& a, const BigInt& b, bool negative) {
        BigInt result(nullptr, negative);

        const Node* previousA = a.tail;
        const Node* previousB = b.tail;
        int carry = 0;
        
        // Add the digits from the least significant to the most significant, handling carry
        while (previousA || previousB || carry) {
            const int sum = 
                   carry 
                + (previousA ? previousA->digit : 0)                                    // ? Ensure we can add numbers of different 
                + (previousB ? previousB->digit : 0)                                    // ? lengths by treating missing digits as 0
            ;
            
            // Prepend the digit to the result
            result.prependDigit(static_cast<signed char>(sum % 10));

            // Update the carry for the next iteration
            carry = sum / 10;

            // Move to the next more significant digit
            if (previousA) previousA = previousA->previous;
            if (previousB) previousB = previousB->previous;
        }

        return result;
    }

    
    // Subtract magnitudes, |a| - |b|, digit by digit from the LSD ends
    // Note: requires that |a| >= |b| which the caller enforces via compare
    static BigInt subtractMagnitudes(const BigInt& a, const BigInt& b, bool negative) {
        BigInt result(nullptr, negative);

        const Node* previousA = a.tail;
        const Node* previousB = b.tail;
        int borrow = 0;
        
        // Subtract the digits from the least significant to the most significant, handling borrowing
        while (previousA) {
            int difference =
                   previousA->digit 
                -  borrow 
                - (previousB ? previousB->digit : 0)                                    // ? Ensure we can subtract numbers of different
            ;                                                                           // ? lengths by treating missing digits as 0
            
            // Handle borrowing if the difference is negative
            if (difference < 0) { 
                difference += 10; 
                borrow = 1; 
            } 
            else    borrow = 0;
            
            // Prepend the digit to the result
            result.prependDigit(static_cast<signed char>(difference));

            // Move to the next more significant digit
            previousA = previousA->previous;
            if (previousB) previousB = previousB->previous;
        }

        // Strip leading zeros. E.g: 100 - 99, which builds "001", and is converted to "1"
        while (result.head && result.head->digit == 0 && result.head->next) {
            result.head = std::move(result.head->next);                                 // ? Safe: release() runs before the old head is deleted due to moving
            result.head->previous = nullptr;                                            // ? Safe: the new head has no previous node, so we set it to nullptr
        }

        return result;
    }


    // Returns -1 / 0 / +1 when comparing |a| vs |b|
    static ComparisonResult compareMagnitudes(const BigInt& a, const BigInt& b) {
        std::size_t lengthA = 0, lengthB = 0;
        
        // Calculate the length of each number
        for (const Node* p = a.head.get(); p; p = p->next.get())    ++lengthA;
        for (const Node* p = b.head.get(); p; p = p->next.get())    ++lengthB;

        // If the lengths are different, the longer must be bigger
        if (lengthA != lengthB) {
            return (                                                                    // ? Compare lengths for cheap exit and to 
                lengthA < lengthB                                                       // ? ensure that A- and B's digits are aligned
                ? ComparisonResult::LESS 
                : ComparisonResult::GREATER
            );           
        }

        // Compare the digits from most significant to least significant 
        const Node* currentA = a.head.get();
        const Node* currentB = b.head.get();

        while (currentA) {
            // Compare the digits
            if (currentA->digit != currentB->digit) {
                return (
                    currentA->digit < currentB->digit 
                    ? ComparisonResult::LESS 
                    : ComparisonResult::GREATER
                ); 
            }
            
            // Move to the next digit
            currentA = currentA->next.get();
            currentB = currentB->next.get();
        }

        return ComparisonResult::EQUAL;
    }

public:
    // * Ctor & dtor
    explicit BigInt(std::string_view number);
    BigInt(const BigInt& other);                                                        // Deep copy ctor needed by operator- overload
    BigInt& operator=(BigInt other) noexcept {                                          // Note: by value!
        // Swap the members
        std::swap(head, other.head);
        std::swap(tail, other.tail);
        std::swap(isNegative, other.isNegative);

        // Return the current object by reference to allow for chained assignments
        return *this;
    }

    // ! Even though the unique_ptr can clean up the linked list from BigInt - 
    // ! which doesn't require a dtor in Node - with the following approach:
    // !    ~Node() {
    // !        while (Node* p = next.get()) {
    // !            next.release();
    // !            std::unique_ptr<Node> chain(p);
    // !        }
    // !    }
    // ! 
    // ! We still want to delete the nodes using a custom dtor in the node struct   
    // ! to ensure we don't blow up the stackframe with recursive dtor calls from
    // ! BigInt. If we don't ensure O(1) auxiliary space complexity for clean up,
    // ! then the recursion would cause memory leaks for huge digit counts. 
    // ! Therefore, clean up is handled by the Node dtor while BigInt uses default 
    ~BigInt() = default;

    // * Overloads
    // Stream insertion operator overload for printing the BigInt
    friend std::ostream& operator<<(std::ostream& os, const BigInt& bigInt) {
        using namespace std::string_view_literals;

        // If the number is zero, print "0"
        if (!bigInt.head) [[unlikely]]  return os << "0"sv;
        
        // If the number is negative, print the negative sign
        if (bigInt.isNegative)  os << "-"sv;


        // Print the digits in linked list order - MSD to LSD
        Node* current = bigInt.head.get();
        while (current) {
            os << static_cast<int>(current->digit);                                     // Cast to int to print the digit correctly
            current = current->next.get();
        }

        return os;
    }

    // Arithmetic addtion operator overload for adding two BigInt objects
    [[nodiscard]] friend BigInt operator+(const BigInt& a, const BigInt& b) {
        if (a.isNegative == b.isNegative) {
            return addMagnitudes(a, b, a.isNegative);
        }

        // Mixed signs: subtract the smaller magnitude from the larger
        const ComparisonResult result = compareMagnitudes(a, b);

        // Determine the result based on the comparison of magnitudes
        switch (result) {
            case ComparisonResult::GREATER: return subtractMagnitudes(a, b, a.isNegative);
            case ComparisonResult::LESS:    return subtractMagnitudes(b, a, b.isNegative);
            default:                        return BigInt(nullptr, false);              // ! Serves to supress warning/UB about no return after the switch
        }                                                                               // ! statement, and in cases where: x + (-x) = 0; - in which it
    }                                                                                   // ! constructs the canonical zero (empty list, positive sign)

    // Arithmetic subtraction operator overload for subtracting two BigInt objects
    [[nodiscard]] friend BigInt operator-(const BigInt& a, const BigInt& b) {
        BigInt negatedB = b;                                                            // Deep copy, then flip the sign
        negatedB.isNegative = !negatedB.isNegative;
        
        return a + negatedB;                                                            // ? a - b == a + (-b)
    };
};


// * Implementations
BigInt::BigInt(std::string_view number) {
    using namespace std::string_view_literals;

    // Ensure the string is not empty
    if (number.empty() || number == "-"sv) [[unlikely]] {
        throw std::invalid_argument("Invalid number string");
    }

    // Check if the number is negative
    isNegative = number.starts_with("-"sv);

    // Skip the negative sign, if it exists, by moving the string_view pointer forward 
    if (isNegative) number.remove_prefix(1);

    // Strip leading zeros, if any, by moving the string_view pointer forward
    while (number.starts_with("0"sv) && number.size() > 1) {                            // ? While number starts with "0" and has more than one digit left
        number.remove_prefix(1);
    }

    // Normalize negative zero: a zero magnitude is never negative
    if (number == "0"sv) [[unlikely]]   isNegative = false;


    // Initialize the next node after the tail (which is none)                      
    std::unique_ptr<Node> nextNode = nullptr;                                       
    Node* previousNode = nullptr;                                                   
    
    // Create a new node for each digit in the number
    // ? NOTES:                                                                
    // ?    Since the previous node is a raw pointer, it allows us to                  
    // ?    utilize dangling pointers to circumvent the issue of requiring             
    // ?    either two O(n) loops, or look up the previous node each 
    // ?    time we add a new node. Avoiding having to traverse the entire  
    // ?    list to find the previous node each time we add a new node 
    // ?    makes the ctor go from O(n^2) to O(n) in time complexity. 
    // ?
    // ?    We reverse the string, and construct the linked list from the end, because
    // ?    we need to store the head as an owning member within the BigInt object for the 
    // ?    linked list to be of any use. This is only true for the direction of the owning,
    // ?    and because of this, it allows us to simply disregard the pointer to the last node 
    // ?    after it's assigned to its predecessor. Therefore, we want to make the retreival 
    // ?    of the head as simple as possible, which is why we simply construct the linked 
    // ?    list in reverse order, so the final value of nextNode is the head of the linked   
    // ?    list. We would also have to do digit assignment once outside of the loop, which   
    // ?    is reeaaally bad practice, as it spreads similar logic to different places. 
    // ?           
    for (char c : std::views::reverse(number)) {                                        // views::reverse reads backwards; the underlying, possibly read-only, data is never modified
        // Check if the digit is valid                                              
        if (c < '0' || c > '9') {
            throw std::invalid_argument("Invalid character in number string");
        }

        // Convert char to signed char
        const auto digit = static_cast<signed char>(c - '0');
        
        // Create a new node and link it to it's successor node                         // ? The Node doesn't get automatically destructed by the unique_ptr dtor
        nextNode = std::unique_ptr<Node>(new Node{                                      // ? here even though it goes out of scope, this is because ownership of 
            .next = std::move(nextNode),                                                // ? the pointer is transferred to the next node, which is still in scope. 
            .previous = previousNode,                                                   // ? The previous pointer lacks ownership as the preceeding node always should outlive it
            .digit = digit  
        }); 
                                                                                        // ? This is a dangling pointer, but it's safe to use here because the nextNode 
        // Update the previous node pointer to the current node                         // ? unique_ptr owns the memory and will keep it alive until the next iteration 
        previousNode = nextNode.get();                                                  // ? of the loop, at which point ownership is transferred to the next node.

        // Set the tail pointer to the least significant digit node 
        if (!tail) tail = previousNode;                                                 // The least significant digit is the first node created
    }                                                                                   

    // Store the head of the linked list    
    head = std::move(nextNode);                                                         // ? nextNode here is seen from the perspective of the node before the head
}

BigInt::BigInt(const BigInt& other) : isNegative(other.isNegative) {
    // ? Pointer-to-pointer idiom: build the new linked list while keeping track of the previous node 
    Node* previous = nullptr;
    std::unique_ptr<Node>* link = &head;                                                // Pointer to the unique_ptr member
    
    // Iterate through the other BigInt's linked list and copy each node
    for (const Node* p = other.head.get(); p; p = p->next.get()) {
        // Create a new node and link it to the current node
        *link = std::unique_ptr<Node>(new Node{                                         // Assign through the unique_ptr*
            .next = nullptr,
            .previous = previous,
            .digit = p->digit
        });

        // Update the previous pointer for the next iteration
        previous = link->get();
        link = &previous->next;
    }

    // Set the tail pointer to the last node in the copied list
    tail = previous;
}


int main() {
    using namespace std::string_view_literals;

    // Get a number from the user
    std::cout << "Enter a number: "sv;
    std::string input;
    std::cin >> input;

    // Create a BigInt object from the input
    BigInt inputInt(input);
    std::cout << "inputInt: "sv << inputInt << "\n"sv;

    // Test all scenarios with a 20+ digit number
    BigInt intB("12345678901234567890"sv);
    BigInt intC("-98765432109876543210"sv);

    std::cout << "    intB: "sv << intB << "\n"sv;
    std::cout << "    intC: "sv << intC << "\n"sv;

    std::cout << "\n"sv << "Addtition:"sv << "\n"sv;
    std::cout << "(inputInt + intB) Sum: "sv << inputInt + intB << "\n"sv;
    std::cout << "(inputInt + intC) Sum: "sv << inputInt + intC << "\n"sv;
    std::cout << "(intB + inputInt) Sum: "sv << intB + inputInt << "\n"sv;
    std::cout << "(intC + inputInt) Sum: "sv << intC + inputInt << "\n"sv;
    std::cout << "    (intB + intB) Sum: "sv << intB + intB << "\n"sv;
    std::cout << "    (intB + intC) Sum: "sv << intB + intC << "\n"sv;
    std::cout << "    (intC + intB) Sum: "sv << intC + intB << "\n"sv;
    std::cout << "    (intC + intC) Sum: "sv << intC + intC << "\n"sv;

    std::cout << "\n"sv << "Subtraction:"sv << "\n"sv;
    std::cout << "(inputInt - intB) Difference: "sv << inputInt - intB << "\n"sv;
    std::cout << "(inputInt - intC) Difference: "sv << inputInt - intC << "\n"sv;
    std::cout << "(intB - inputInt) Difference: "sv << intB - inputInt << "\n"sv;
    std::cout << "(intC - inputInt) Difference: "sv << intC - inputInt << "\n"sv;
    std::cout << "    (intB - intB) Difference: "sv << intB - intB << "\n"sv;
    std::cout << "    (intB - intC) Difference: "sv << intB - intC << "\n"sv;
    std::cout << "    (intC - intB) Difference: "sv << intC - intB << "\n"sv;
    std::cout << "    (intC - intC) Difference: "sv << intC - intC << "\n"sv;

    return 0;
}