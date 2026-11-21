// Tour of dbg(): run it with `make run FILE=src/examples/dbg_demo.cpp`.
import std;
#include <numbers>

#include "dbg.h"  // optional: the Makefile already force-includes it

struct Point {
  double x;
  double y;
};

struct Node {  // a tiny linked list
  int value;
  Node* next;
};

enum class Color { kRed, kGreen, kBlue };

class Date {  // private members are shown too
 public:
  Date(int y, int m, int d) : y_{y}, m_{m}, d_{d} {}
  int Year() const { return y_; }
  int Month() const { return m_; }
  int Day() const { return d_; }

 private:
  int y_, m_, d_;
};

struct Money {  // a type with its own operator<< is printed with it
  long cents;
};
std::ostream& operator<<(std::ostream& os, Money m) {
  return os << '$' << m.cents / 100 << '.' << std::setw(2) << std::setfill('0')
            << m.cents % 100;
}

int Square(int n) { return n * n; }

int main() {
  dbg();  // just "file:line (function)"

  int answer = 42;
  double pi = std::numbers::pi;
  char c = 'A';
  bool ok = true;
  std::string name = "Bjarne";
  dbg(answer, pi, c, ok, name);

  std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};
  int arr[] = {10, 20, 30};
  std::array<std::string, 2> words{"hello", "world"};
  dbg(v, arr, words);

  std::vector<std::vector<int>> matrix{{1, 2, 3}, {40, 50, 60}, {700, 8, 9}};
  double grid[2][3] = {{1.5, 2, 3}, {4, 5.25, 6}};
  dbg(matrix);
  dbg(grid);

  std::map<std::string, int> score{{"alice", 10}, {"bob", 7}};
  std::set<char> letters{'c', 'a', 'b'};
  std::pair<int, std::string> pr{1, "one"};
  std::tuple<int, double, char> tup{1, 2.5, 'z'};
  dbg(score, letters, pr, tup);

  Point p{.x = 1.5, .y = -2};
  std::vector<Point> path{{.x = 0, .y = 0}, {.x = 1, .y = 1}};
  dbg(p, path);

  Node third{.value = 3, .next = nullptr}, second{.value = 2, .next = &third},
      first{.value = 1, .next = &second};
  Node* head = &first;
  int* null_ptr = nullptr;
  dbg(head, &answer, null_ptr);

  auto up = std::make_unique<Point>(Point{.x = 7, .y = 8});
  auto sp = std::make_shared<int>(99);
  dbg(up, sp);

  std::optional<int> maybe = 5, nothing;
  std::variant<int, std::string> var = "text";
  dbg(maybe, nothing, var);

  Color col = Color::kGreen;
  dbg(col);

  std::stack<int> st;
  st.push(1);
  st.push(2);
  dbg(st);

  dbg(Date{2024, 3, 14});
  dbg(Money{1999});

  // dbg returns its argument, so you can wrap any expression:
  int y = dbg(Square(answer)) + 1;
  dbg(y);
}
