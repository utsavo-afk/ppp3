// Plain #includes work too, mixed with `import std;` in any order.
#include <iostream>
#include <map>
#include <string>

#include "dbg.h"  // optional: the Makefile already force-includes it

int main() {
  std::map<std::string, int> ages{{"Ada", 36}, {"Bjarne", 74}};
  dbg(ages);
  std::cout << "Bjarne is " << ages["Bjarne"] << '\n';
}
