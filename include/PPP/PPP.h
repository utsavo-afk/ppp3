// PPP.h -- "Programming: Principles and Practice Using C++" (3rd ed.)
//
// [ppp3 local change] The published PPP.h is just:
//     import PPP; using namespace PPP; using namespace std; + the #defines
// clangd 22 crashes when an `import` is hidden inside a header, so this copy
// pulls in exactly the same code textually instead (= PPPheaders.h).
// You can still write `import PPP;` yourself at the top of a .cpp file.

#include "PPPheaders.h"
