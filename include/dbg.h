// dbg.h - a Rust-style dbg() for C++20 (clang + libc++).
//
//   dbg(x);                  // [src/ch02/main.cpp:7 (main)] x = 42  int
//   dbg(v, m, p);            // several values, one line each
//   int y = dbg(f(x)) + 1;   // dbg(expr) returns expr, so it can wrap anything
//   dbg();                   // just prints "file:line (function)"
//
// It prints (to stderr, colored when stderr is a terminal):
//   numbers, bool, char, strings, enums (by name), raw/smart pointers
//   (address -> pointee, follows linked structures), std::optional,
//   std::variant, pairs/tuples, every range (std::vector, std::array,
//   C arrays, std::list, std::map, std::set, ...), container adapters
//   (std::stack/queue/priority_queue), 2-D ranges as an aligned matrix,
//   structs with public members as Type{.field = value, ...}, classes
//   with private members, and anything with an operator<<.
//
// Notes:
//   * The Makefile force-includes this (via ppp_prelude.h) into every
//     program, so you never need to #include it yourself.
//   * This header avoids the identifiers `vector`, `string` and `span` on
//     purpose, so it keeps working after PPP.h's `#define vector ...` hacks.
//   * Define DBG_DISABLE to turn every dbg(...) into a plain pass-through.

#ifndef PPP3_DBG_H
#define PPP3_DBG_H

#include <sys/uio.h>  // process_vm_readv: check that a pointer is readable
#include <unistd.h>   // isatty, getpid

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++26-extensions"  // binding packs
#pragma clang diagnostic ignored "-Wundefined-var-template"
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#pragma clang diagnostic ignored "-Wformat-security"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"

namespace dbg_detail {

using Str = std::basic_string<char>;
using std::string_view;

inline constexpr int kMaxDepth = 8;  // nesting / pointer-chasing limit
inline constexpr std::size_t kMaxElems = 100;  // elements shown per range

// ---------------------------------------------------------------- colors --
inline bool UseColor() {
  static const bool on = ::isatty(2) && std::getenv("NO_COLOR") == nullptr;
  return on;
}
inline const char* Ansi(const char* code) { return UseColor() ? code : ""; }
inline const char* Dim() { return Ansi("\033[2m"); }
inline const char* Bold() { return Ansi("\033[1;36m"); }
inline const char* Green() { return Ansi("\033[32m"); }
inline const char* Reset() { return Ansi("\033[0m"); }

// ------------------------------------------------------------- type names --
inline void ReplaceAll(Str& s, string_view from, string_view to) {
  for (std::size_t p = 0; (p = s.find(from, p)) != Str::npos; p += to.size())
    s.replace(p, from.size(), to);
}

// Removes ", std::allocator<...>" style default template arguments.
inline void EraseDefaultArg(Str& s, string_view head) {
  for (std::size_t p; (p = s.find(head)) != Str::npos;) {
    std::size_t i = p + head.size();
    int depth = 1;
    for (; i < s.size() && depth > 0; ++i) {
      if (s[i] == '<') ++depth;
      if (s[i] == '>') --depth;
    }
    s.erase(p, i - p);
  }
}

inline Str Prettify(Str s) {
  ReplaceAll(s, "std::__1::", "std::");
  ReplaceAll(s, " *", "*");
  ReplaceAll(s, " &", "&");
  EraseDefaultArg(s, ", std::char_traits<");
  EraseDefaultArg(s, ", std::allocator<");
  EraseDefaultArg(s, ", std::less<");
  EraseDefaultArg(s, ", std::hash<");
  EraseDefaultArg(s, ", std::equal_to<");
  EraseDefaultArg(s, ", std::default_delete<");
  ReplaceAll(s, "std::basic_string<char>", "std::string");
  ReplaceAll(s, "std::basic_string_view<char>", "std::string_view");
  ReplaceAll(s, "PPP::Checked_vector", "vector");
  ReplaceAll(s, "PPP::Checked_string", "string");
  ReplaceAll(s, "PPP::Checked_span", "span");
  return s;
}

template <class T>
constexpr string_view RawTypeName() {
  string_view f = __PRETTY_FUNCTION__;  // "... RawTypeName() [T = int]"
  auto b = f.find("[T = ") + 5;
  return f.substr(b, f.rfind(']') - b);
}

template <class T>
const Str& TypeName() {
  static const Str name = Prettify(Str(RawTypeName<T>()));
  return name;
}

// ------------------------------------------------------------------ traits --
template <class T>
using Bare = std::remove_cvref_t<T>;

template <class T>
concept CharLike = std::same_as<T, char>;

template <class T>
concept CharPtr =
    std::same_as<std::remove_cv_t<std::remove_pointer_t<T>>, char> &&
    std::is_pointer_v<T>;

template <class T>
concept CharArray =
    std::is_array_v<T> &&
    std::same_as<std::remove_cv_t<std::remove_extent_t<T>>, char>;

template <class T>
concept StringLike =
    !CharPtr<T> && !CharArray<T> &&
    std::convertible_to<const T&, string_view> && !std::is_pointer_v<T>;

template <class T>
concept Ostreamable = requires(std::ostream& os, const T& t) { os << t; };

template <class T>
concept SmartPtr = requires(const T& p) {
  typename T::element_type;
  { p.get() } -> std::convertible_to<const volatile void*>;
};

template <class T>
concept Optional = requires(const T& o) {
  typename T::value_type;
  { o.has_value() } -> std::same_as<bool>;
  *o;
  requires std::same_as<Bare<T>, std::optional<typename T::value_type>>;
};

template <class T>
struct IsVariant : std::false_type {};
template <class... Ts>
struct IsVariant<std::variant<Ts...>> : std::true_type {};

template <class T>
concept TupleLike =
    !std::ranges::range<T> && requires { std::tuple_size<T>::value; };

template <class T>
concept MapLike = std::ranges::range<T> && requires {
  typename T::key_type;
  typename T::mapped_type;
};

template <class T>
concept SetLike =
    std::ranges::range<T> && requires { typename T::key_type; } && !MapLike<T>;

template <class T>
concept Range = std::ranges::range<const T> && !StringLike<T> && !CharArray<T>;

template <class T>
concept Adapter = requires { typename T::container_type; } && !Range<T>;

template <class T>
concept Bindable = std::is_class_v<T> && !std::is_union_v<T> &&
                   requires { __builtin_structured_binding_size(T); };

// A "matrix" is a range whose elements are ranges of non-range things.
template <class T>
concept Matrix =
    Range<T> && Range<Bare<std::ranges::range_reference_t<const T>>> &&
    !Range<Bare<std::ranges::range_reference_t<
        Bare<std::ranges::range_reference_t<const T>>>>>;

// ----------------------------------------------------- struct field names --
template <class T>
extern const T kFake;  // never defined; only its members' addresses are used

template <class P>
struct Wrap {
  const P* p;
};

template <class T>
consteval auto FieldPtrs() {
  const auto& [... fs] = kFake<T>;
  return std::tuple{Wrap<Bare<decltype(fs)>>{&fs}...};
}

template <auto W>
consteval string_view RawFieldName() {
  return __PRETTY_FUNCTION__;  // "... [W = ...{&kFake.field}]"
}

template <class T, std::size_t I>
string_view FieldName() {
  string_view f = RawFieldName<std::get<I>(FieldPtrs<T>())>();
  auto e = f.rfind('}');
  auto b = f.rfind('.', e) + 1;
  return f.substr(b, e - b);
}

// --------------------------------------------------------- enum value names --
template <auto V>
consteval string_view RawEnumName() {
  return __PRETTY_FUNCTION__;  // "... [V = Color::red]" or "[V = (Color)7]"
}

template <class E, int V>
string_view EnumName() {
  string_view f = RawEnumName<std::bit_cast<E>(
      static_cast<std::underlying_type_t<E>>(V))>();
  auto b = f.find("[V = ") + 5;
  auto n = f.substr(b, f.rfind(']') - b);
  return n.starts_with('(') ? string_view{} : n;  // "(Color)7": not a name
}

template <class E, int... Is>
string_view EnumLookup(long long v, std::integer_sequence<int, Is...>) {
  string_view result;
  ((v == Is ? (result = EnumName<E, Is>(), 0) : 0), ...);
  return result;
}

// --------------------------------------------------------------- helpers --
inline bool Readable(const void* p, std::size_t n) {
  if (p == nullptr) return false;
  char buf[64];
  ::iovec local{buf, n < sizeof buf ? n : sizeof buf};
  ::iovec remote{const_cast<void*>(p), local.iov_len};
  return ::process_vm_readv(::getpid(), &local, 1, &remote, 1, 0) ==
         static_cast<::ssize_t>(local.iov_len);
}

inline Str Addr(const volatile void* p) {
  return std::format("{}", const_cast<const void*>(p));
}

inline void Quote(Str& out, string_view s, char q) {
  out += q;
  for (char c : s) {
    switch (c) {
      case '\n':
        out += "\\n";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\0':
        out += "\\0";
        break;
      case '\\':
        out += "\\\\";
        break;
      default:
        if (c == q) {
          out += '\\';
          out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
          out += std::format("\\x{:02x}", static_cast<unsigned char>(c));
        } else {
          out += c;
        }
    }
  }
  out += q;
}

template <class T>
void Fmt(Str& out, const T& x, int depth);

template <class R>
void FmtSeq(Str& out, const R& r, int depth, const char* open,
            const char* close) {
  out += open;
  std::size_t n = 0;
  for (auto&& e : r) {
    if (n == kMaxElems) {
      out +=
          std::format(", ... ({} more)",
                      static_cast<std::size_t>(std::ranges::distance(r)) - n);
      break;
    }
    if (n++) out += ", ";
    if constexpr (std::same_as<std::ranges::range_value_t<const R>, bool>)
      out += static_cast<bool>(e) ? "true" : "false";  // std::vector<bool>
    else
      Fmt(out, e, depth + 1);
  }
  out += close;
}

// Expose the protected container inside std::stack / queue / priority_queue.
template <class A>
const auto& AdapterContainer(const A& a) {
  struct Peek : A {
    static const typename A::container_type& Get(const A& x) {
      return x.*(&Peek::c);
    }
  };
  return Peek::Get(a);
}

// Callback for __builtin_dump_struct (classes with private members).
struct DumpSink {
  Str* out;
  template <class... Args>
  int operator()(const char* fmt, Args... args) const {
    char buf[512];
    int n = std::snprintf(buf, sizeof buf, fmt, args...);
    if (n > 0) out->append(buf, std::min<std::size_t>(n, sizeof buf - 1));
    return n;
  }
};

// Turns clang's multi-line dump ("Date {\n  int y_ = 2024\n}") into
// "Date{.y_ = 2024}", matching the style used for plain structs.
inline Str CompactDump(string_view raw) {
  Str out;
  bool need_comma = false;
  while (!raw.empty()) {
    auto nl = raw.find('\n');
    string_view line = raw.substr(0, nl);
    raw = nl == string_view::npos ? string_view{} : raw.substr(nl + 1);
    while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
    if (line.empty()) continue;
    if (line == "}") {
      out += '}';
      need_comma = true;
      continue;
    }
    if (need_comma) out += ", ";
    need_comma = false;
    auto eq = line.find(" = ");
    if (eq != string_view::npos) {  // "<type> <name> = <value>"
      auto sp = line.rfind(' ', eq - 1);
      string_view type = sp == string_view::npos ? "" : line.substr(0, sp);
      string_view name =
          line.substr(sp == string_view::npos ? 0 : sp + 1,
                      eq - (sp == string_view::npos ? 0 : sp + 1));
      string_view value = line.substr(eq + 3);
      out += '.';
      out += name;
      out += " = ";
      if (value == "{") {  // nested class member
        out += type;
        out += '{';
      } else {
        out += value;
        need_comma = true;
      }
    } else if (line.ends_with(" {")) {  // "Type {" (the object or a base)
      line.remove_suffix(2);
      out += line;
      out += '{';
    } else {
      out += line;
      need_comma = true;
    }
  }
  return out;
}

// ------------------------------------------------------- the formatter --
template <class T>
void Fmt(Str& out, const T& x, int depth) {
  if (depth > kMaxDepth) {
    out += "...";
  } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
    out += "nullptr";
  } else if constexpr (std::is_same_v<T, bool>) {
    out += x ? "true" : "false";
  } else if constexpr (CharLike<T>) {
    Quote(out, string_view(&x, 1), '\'');
  } else if constexpr (CharPtr<T>) {
    if (x == nullptr) {
      out += "nullptr";
    } else if (!Readable(x, 1)) {
      out += Addr(x) + " (unreadable!)";
    } else {
      Quote(out, x, '"');
    }
  } else if constexpr (CharArray<T>) {
    Quote(out, string_view(x, std::find(x, x + std::extent_v<T>, '\0')), '"');
  } else if constexpr (StringLike<T>) {
    Quote(out, string_view(x), '"');
  } else if constexpr (std::is_same_v<T, signed char> ||
                       std::is_same_v<T, unsigned char>) {
    out += std::format("{}", static_cast<int>(x));
  } else if constexpr (std::is_arithmetic_v<T>) {
    out += std::format("{}", x);
  } else if constexpr (std::is_enum_v<T>) {
    auto v = static_cast<long long>(static_cast<std::underlying_type_t<T>>(x));
    auto name = EnumLookup<T>(v, std::make_integer_sequence<int, 64>{});
    if (name.empty())
      out += std::format("({}){}", TypeName<T>(), v);
    else
      out += std::format("{} ({})", name, v);
  } else if constexpr (SmartPtr<T>) {
    if (!x) {
      out += "nullptr";
    } else {
      out += Addr(x.get());
      if constexpr (requires { x.use_count(); })
        out += std::format(" (use_count={})", x.use_count());
      if constexpr (!std::is_void_v<typename T::element_type>) {
        out += " -> ";
        Fmt(out, *x, depth + 1);
      }
    }
  } else if constexpr (std::is_pointer_v<T>) {
    using P = std::remove_cv_t<std::remove_pointer_t<T>>;
    if (x == nullptr) {
      out += "nullptr";
    } else if constexpr (std::is_void_v<P> || std::is_function_v<P>) {
      out += Addr(reinterpret_cast<const volatile void*>(x));
    } else {
      out += Addr(x);
      if (Readable(x, sizeof(P))) {
        out += " -> ";
        Fmt(out, *x, depth + 1);
      } else {
        out += " (dangling/unreadable!)";
      }
    }
  } else if constexpr (Optional<T>) {
    if (x.has_value()) {
      out += "optional(";
      Fmt(out, *x, depth + 1);
      out += ")";
    } else {
      out += "nullopt";
    }
  } else if constexpr (IsVariant<T>::value) {
    if (x.valueless_by_exception()) {
      out += "valueless";
    } else {
      std::visit(
          [&](const auto& v) {
            Fmt(out, v, depth + 1);
            out += std::format(" ({})", TypeName<Bare<decltype(v)>>());
          },
          x);
    }
  } else if constexpr (Ostreamable<T> && !std::is_array_v<T>) {
    std::ostringstream os;
    os << x;
    out += os.str();
  } else if constexpr (MapLike<T>) {
    out += "{";
    std::size_t n = 0;
    for (const auto& [k, v] : x) {
      if (n == kMaxElems) {
        out += ", ...";
        break;
      }
      if (n++) out += ", ";
      Fmt(out, k, depth + 1);
      out += ": ";
      Fmt(out, v, depth + 1);
    }
    out += "}";
  } else if constexpr (SetLike<T>) {
    FmtSeq(out, x, depth, "{", "}");
  } else if constexpr (Range<T>) {
    FmtSeq(out, x, depth, "[", "]");
  } else if constexpr (Adapter<T>) {
    FmtSeq(out, AdapterContainer(x), depth, "[", "]");
  } else if constexpr (TupleLike<T>) {
    out += "(";
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      ((out += (Is ? ", " : ""), Fmt(out, std::get<Is>(x), depth + 1)), ...);
    }(std::make_index_sequence<std::tuple_size_v<T>>{});
    out += ")";
  } else if constexpr (Bindable<T>) {
    const auto& [... fs] = x;
    out += TypeName<T>();
    out += "{";
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      ((out += (Is ? ", ." : "."), out += FieldName<T, Is>(), out += " = ",
        Fmt(out, fs...[Is], depth + 1)),
       ...);
    }(std::make_index_sequence<sizeof...(fs)>{});
    out += "}";
  } else if constexpr (std::is_class_v<T>) {
    // Private members: let clang walk the fields for us.
    Str raw;
    __builtin_dump_struct(&x, DumpSink{&raw});
    out += Prettify(CompactDump(raw));
  } else {
    out += "<" + TypeName<T>() + ">";
  }
}

// Top-level 2-D ranges get an aligned, multi-line layout.
template <class M>
Str FmtMatrix(const M& m, Str& dims) {
  std::size_t rows = 0, cols = 0;
  bool jagged = false;
  constexpr std::size_t kMaxCols = 64, kMaxRows = 64;
  std::size_t widths[kMaxCols] = {};
  Str cell, joined;

  // First pass: widths.
  for (auto&& row : m) {
    std::size_t c = 0;
    for (auto&& e : row) {
      if (c < kMaxCols) {
        cell.clear();
        Fmt(cell, e, 1);
        widths[c] = std::max(widths[c], cell.size());
      }
      ++c;
    }
    if (rows == 0)
      cols = c;
    else if (c != cols)
      jagged = true;
    cols = std::max(cols, c);
    ++rows;
  }
  dims = jagged ? std::format("{} rows, jagged", rows)
                : std::format("{}x{}", rows, cols);

  // Second pass: render.
  std::size_t r = 0;
  joined += "[";
  for (auto&& row : m) {
    if (r == kMaxRows) {
      joined += std::format(",\n  ... ({} more rows)", rows - r);
      break;
    }
    if (r) joined += ",\n ";
    joined += "[";
    std::size_t c = 0;
    for (auto&& e : row) {
      if (c == kMaxCols) {
        joined += ", ...";
        break;
      }
      if (c) joined += ", ";
      cell.clear();
      Fmt(cell, e, 1);
      joined.append(widths[c] - cell.size(), ' ');
      joined += cell;
      ++c;
    }
    joined += "]";
    ++r;
  }
  joined += "]";
  return joined;
}

// Splits "a, f(b, c), d" at top-level commas.
inline std::array<string_view, 32> SplitNames(string_view s, std::size_t& n) {
  std::array<string_view, 32> parts{};
  n = 0;
  int depth = 0;
  char quote = 0;
  std::size_t start = 0;
  auto push = [&](std::size_t end) {
    auto p = s.substr(start, end - start);
    while (!p.empty() && p.front() == ' ') p.remove_prefix(1);
    while (!p.empty() && p.back() == ' ') p.remove_suffix(1);
    if (n < parts.size()) parts[n] = p;
    ++n;
  };
  for (std::size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (quote) {
      if (c == '\\')
        ++i;
      else if (c == quote)
        quote = 0;
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '(' || c == '[' || c == '{') {
      ++depth;
    } else if (c == ')' || c == ']' || c == '}') {
      --depth;
    } else if (c == ',' && depth == 0) {
      push(i);
      start = i + 1;
    }
  }
  if (!s.empty()) push(s.size());
  return parts;
}

template <class T>
void PrintOne(Str& out, string_view loc, string_view name, const T& x) {
  using U = Bare<T>;
  Str type = TypeName<U>();
  if constexpr (Matrix<U> && !MapLike<U>) {
    Str dims;
    Str body = FmtMatrix(x, dims);
    out += std::format("{}{}{} {}{}{} = {}{} [{}]{}\n", Dim(), loc, Reset(),
                       Bold(), name, Reset(), Green(), type, dims, Reset());
    out += "    ";
    for (char c : body) {
      out += c;
      if (c == '\n') out += "    ";
    }
    out += '\n';
  } else {
    Str val;
    Fmt(val, x, 0);
    if (val.find('\n') != Str::npos) {  // multi-line (e.g. private-field dump)
      out += std::format("{}{}{} {}{}{} = {}{}{}\n", Dim(), loc, Reset(),
                         Bold(), name, Reset(), Green(), type, Reset());
      out += "    ";
      for (char c : val) {
        out += c;
        if (c == '\n') out += "    ";
      }
      out += '\n';
    } else {
      out += std::format("{}{}{} {}{}{} = {}  {}{}{}\n", Dim(), loc, Reset(),
                         Bold(), name, Reset(), val, Green(), type, Reset());
    }
  }
}

template <class T>
using Ret = std::conditional_t<std::is_lvalue_reference_v<T>, T, Bare<T>>;

template <class... Ts>
decltype(auto) Run(const char* file, int line, const char* func,
                   string_view exprs, Ts&&... xs) {
  Str loc = std::format("[{}:{} ({})]", file, line, func);
  Str out;
  if constexpr (sizeof...(Ts) == 0) {
    out = std::format("{}{}{}\n", Dim(), loc, Reset());
  } else {
    std::size_t n = 0;
    auto names = SplitNames(exprs, n);
    bool ok = n == sizeof...(Ts);
    std::size_t i = 0;
    auto one = [&](const auto& x) {
      Str fallback = std::format("arg{}", i);
      PrintOne(out, loc, ok ? names[i] : string_view(fallback), x);
      ++i;
    };
    (one(xs), ...);
  }
  std::cout.flush();  // keep stdout/stderr output in order
  std::cerr << out;
  if constexpr (sizeof...(Ts) == 1) {
    return (Ret<Ts>(std::forward<Ts>(xs)), ...);
  }
}

template <class... Ts>
decltype(auto) PassThrough(Ts&&... xs) {
  if constexpr (sizeof...(Ts) == 1) return (Ret<Ts>(std::forward<Ts>(xs)), ...);
}

}  // namespace dbg_detail

#pragma clang diagnostic pop

#ifdef DBG_DISABLE
#define dbg(...) ::dbg_detail::PassThrough(__VA_ARGS__)
#else
#define dbg(...)                                  \
  ::dbg_detail::Run(__FILE__, __LINE__, __func__, \
                    #__VA_ARGS__ __VA_OPT__(, ) __VA_ARGS__)
#endif

#endif  // PPP3_DBG_H
