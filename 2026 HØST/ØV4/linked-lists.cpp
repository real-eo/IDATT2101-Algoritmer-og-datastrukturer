//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>



struct Node {
    std::unique_ptr<Node> next;                                                         // ? Reduce memory usage by declaring the bigger data types first
    Node* previous;                                                                     // ! USE RAW POINTERS FOR PREVIOUS POINTERS! All types of smart pointers    
    signed char digit;                                                                  // ! would be incorrect here. A shared pointer would cause unnecessary 
};                                                                                      // ! overhead, along with impling ownership, like a unique pointer, which
                                                                                        // ! is wrong, and a weak pointer requires a shared pointer to be valid. A 
                                                                                        // ! reference is also wrong as it can't be null, which we need for the head.
class BigInt {
private:
    std::unique_ptr<Node> head;
    bool isNegative;

public:
    explicit BigInt(std::string number) {
        // Ensure the string is not empty
        if (number.empty() || number == "-") {
            throw std::invalid_argument("Invalid number string");
        }

        // Check if the number is negative
        isNegative = number.starts_with('-');

        // Skip the negative sign if it exists
        if (isNegative) number = number.substr(1);
        
        // Reverse the string                                                           // ? We reverse the string, and construct the linked list from the end, because
        std::reverse(number.begin(), number.end());                                     // ? we need to store the head as a member within the BigInt object for the linked 
                                                                                        // ? list to be of any use. This is only true for the first node (the head), and 
        // Initialize the next node after the tail (which is none)                      // ? because of this, allows us to simply disregard the pointer the last node after
        std::unique_ptr<Node> nextNode = nullptr;                                       // ? it's assigned to its predecessor. Therefore, we want to make the retreival of
        Node* previousNode = nullptr;                                                   // ? the head as simple as possible, which is why we simply construct the linked 
                                                                                        // ? list in reverse order, so the final value of nextNode is the head of the linked    
        // Create a new node for each digit in the number                               // ? list. We also would have to do digit assignment once outside of the loop, which                                                
        // ? Since the previous node is a raw pointer, it allows us to                  // ? is reeaaally bad practice, as it spreads similar logic to different places.
        // ? utilize dangling pointers to circumvent the issue of requiring             
        // ? either two O(n) loops, or look up the previous node each 
        // ? time we add a new node. Avoiding having to traverse the entire  
        // ? list to find the previous node each time we add a new node 
        // ? makes the ctor go from O(n^2) to O(n) in time complexity. 
        for (char c : number) {
            // Check if the digit is valid                                              
            if (c < '0' || c > '9') {
                throw std::invalid_argument("Invalid character in number string");
            }

            // Convert char to signed char
            const auto digit = static_cast<signed char>(c - '0');
            
            // Create a new node and link it to it's successor node                     // ? The Node doesn't get automatically destructed by the unique_ptr dtor
            nextNode = std::make_unique<Node>(Node{                                     // ? here even though it goes out of scope, this is because ownership of 
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

    ~BigInt();
};

// TODO: Implement the destructor 
BigInt::~BigInt() {
    // ! Even though the unique_ptr can automatically clean up the linked list, 
    // ! with either of the following approaches:
    // !    ~Node() {
    // !        Node* p = next.release();
    // !        while (p) p = std::unique_ptr<Node>(std::exchange(p, nullptr)->next).release();
    // !    }
    // !
    // ! or:
    // !    ~Node() {
    // !        while (Node* p = next.get()) {
    // !            next.release();
    // !            std::unique_ptr<Node> chain(p);
    // !        }
    // !    }
    // ! 
    // ! We still want to recursively manually delete the nodes using a loop with 
    // ! O(1) auxiliary space complexity to ensure we don't blow up the stackframe 
    // ! with a recursive dtor call, which would lead to memory leaks on large numbers

}

int main() {
    // Get a number from the user
    std::cout << "Enter a number: ";
    std::string input;
    std::cin >> input;


}