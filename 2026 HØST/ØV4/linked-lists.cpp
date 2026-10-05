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
};


class BigInt {
private:
    std::unique_ptr<Node> head;                                                         // Head is the most significant digit, and the tail is the least significant digit
    bool isNegative;

public:
    // * Ctor & dtor
    explicit BigInt(std::string_view number);
    ~BigInt();

    // * Overloads
    // Stream insertion operator overload for printing the BigInt
    friend std::ostream& operator<<(std::ostream& os, const BigInt& bigInt) {
        using namespace std::string_view_literals;
        
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
        
    }

    // Arithmetic subtraction operator overload for subtracting two BigInt objects
    [[nodiscard]] friend BigInt operator-(const BigInt& a, const BigInt& b);
};


// * Implementations
BigInt::BigInt(std::string_view number) {
    using namespace std::string_view_literals;

    // Ensure the string is not empty
    if (number.empty() || number == "-"sv) {
        throw std::invalid_argument("Invalid number string");
    }

    // Check if the number is negative
    isNegative = number.starts_with("-"sv);

    // Skip the negative sign, if it exists, by moving the string_view pointer forward 
    if (isNegative) number.remove_prefix(1);

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
    // ?    we need to store the head as a member within the BigInt object for the linked 
    // ?    list to be of any use. This is only true for the first node (the head), and 
    // ?    because of this, allows us to simply disregard the pointer the last node after
    // ?    it's assigned to its predecessor. Therefore, we want to make the retreival of
    // ?    the head as simple as possible, which is why we simply construct the linked 
    // ?    list in reverse order, so the final value of nextNode is the head of the linked   
    // ?    list. We also would have to do digit assignment once outside of the loop, which   
    // ?    is reeaaally bad practice, as it spreads similar logic to different places. 
    // ?           
    for (char c : std::views::reverse(number)) {                                    // views::reverse reads backwards; the underlying, possibly read-only, data is never modified
        // Check if the digit is valid                                              
        if (c < '0' || c > '9') {
            throw std::invalid_argument("Invalid character in number string");
        }

        // Convert char to signed char
        const auto digit = static_cast<signed char>(c - '0');
        
        // Create a new node and link it to it's successor node                     // ? The Node doesn't get automatically destructed by the unique_ptr dtor
        nextNode = std::unique_ptr<Node>(new Node{                                  // ? here even though it goes out of scope, this is because ownership of 
            .next = std::move(nextNode),                                            // ? the pointer is transferred to the next node, which is still in scope. 
            .previous = previousNode,                                               // ? The previous pointer lacks ownership as the preceeding node always should outlive it
            .digit = digit
        });
                                                                                    // ? This is a dangling pointer, but it's safe to use here because the nextNode 
        // Update the previous node pointer to the current node                     // ? unique_ptr owns the memory and will keep it alive until the next iteration 
        previousNode = nextNode.get();                                              // ? of the loop, at which point ownership is transferred to the next node.
    }                                                                               
                                                                                    
    // Store the head of the linked list
    head = std::move(nextNode);                                                     // ? nextNode here is seen from the perspective of the node before the head
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
BigInt::~BigInt() = default;


int main() {
    using namespace std::string_view_literals;

    // Get a number from the user
    std::cout << "Enter a number: "sv;
    std::string input;
    std::cin >> input;

    // Create a BigInt object from the input
    BigInt bigInt(input);
    std::cout << "You entered: "sv << bigInt << "\n"sv;
    


}