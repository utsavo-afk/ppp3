// Using the book's support header: PPP.h gives you std, the range-checked
// vector/string, error(), narrow<>(), random_int() ... and dbg().
#include "PPP.h"

int main() {
  vector<int> v = {1, 2, 3};
  dbg(v);
  try {
    v[3] = 7;  // PPP's vector checks every [] -> throws out_of_range
  } catch (const exception& e) {
    cerr << "caught: " << e.what() << '\n';
  }

  int x = narrow<int>(42.0);
  dbg(x, random_int(1, 6));
}
