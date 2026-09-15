#include <iostream>
#include "math_mod.h"
#include "utils.h"

int main()
{
    print_banner();
    std::cout << "3 + 6 = " << add(3, 6) << "\n";
    std::cout << "3 * 5 = " << multiply(3, 5) << "\n";
    return 0;
}