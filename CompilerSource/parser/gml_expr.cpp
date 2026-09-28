// The legacy parser rewrites code as flat text, so GML's grouping and
// evaluation rules never existed as a tree. By print time the text is C++
// shaped (value-position = is already ==, a.b is an accessor call), so it can be
// parsed as expressions here and printed back with GML's rules. The runtime
// helpers are in ENIGMAsystem/SHELL/Universal_System/gml_ops.h.

#include "gml_expr.h"

#include "settings.h"

#include <cctype>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

using std::string;
using std::unique_ptr;
using std::vector;

namespace {

struct Tok {
  string code, synt;
};

// Synt for generated text: names are 'n'; braces and semicolons are 'L' so the
// printer keeps a lambda on one line; other punctuation tags itself.
string syn(const string &c) {
  string s(c);
  for (char &ch : s) {
    if (isalnum((unsigned char) ch) || ch == '_' || ch == ':') ch = 'n';
    else if (ch == '{' || ch == '}' || ch == ';') ch = 'L';
  }
  return s;
}

struct Out {
  string code, synt;
  Out &add(const string &c) { code += c, synt += syn(c); return *this; }
  Out &add(const Out &o) { code += o.code, synt += o.synt; return *this; }
  Out &add(const Tok &t) { code += t.code, synt += t.synt; return *this; }
};

const char *const kOps[] = {"<<=", ">>=", "<<", ">>", "<=", ">=", "==", "!=", "<>", "&&", "||", "++", "--",
                            "->",  "::",  "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^="};

bool is_punct_tok(const string &code, const string &synt, size_t p) {
  const char c = code[p];
  return synt[p] == c && ispunct((unsigned char) c) && c != '"' && c != '\'';
}

vector<Tok> tokenize(const string &code, const string &synt) {
  vector<Tok> toks;
  for (size_t p = 0; p < code.size();) {
    const char s = synt[p];
    size_t n = 1;
    if (is_punct_tok(code, synt, p)) {
      for (const char *op : kOps) {
        const size_t l = strlen(op);
        if (code.compare(p, l, op) == 0 && synt.compare(p, l, op) == 0) { n = l; break; }
      }
    } else if (s != '"' && s != '\'' && code[p] != ' ') {
      while (p + n < code.size() && synt[p + n] == s && !is_punct_tok(code, synt, p + n)) n++;
    }
    toks.push_back({code.substr(p, n), synt.substr(p, n)});
    p += n;
  }
  return toks;
}

bool punct(const Tok &t, const char *p) { return t.code == p && t.synt == p; }
bool space(const Tok &t) { return t.code == " "; }
bool cast(const Tok &t) { return t.synt[0] == 'c' && t.code[0] == '('; }  // "(double)", tagged whole
bool name(const Tok &t) { return t.synt[0] == 'n' || t.synt[0] == 'V' || (t.synt[0] == 'c' && !cast(t)); }
bool number(const Tok &t) { return t.synt[0] == '0'; }
bool str(const Tok &t) { return t.synt[0] == '"' || t.synt[0] == '\''; }
bool scope(const Tok &t) { return punct(t, "::") || t.synt == "XX"; }
bool open2d(const Tok &t) { return t.code == "(" && t.synt == "["; }  // a[i, j], as a(i, j)
bool close2d(const Tok &t) { return t.code == ")" && t.synt == "]"; }

enum class Op { kAssign, kAnd, kOr, kXor, kCmp, kBit, kShift, kArith };
struct BinOp {
  int prec;
  Op kind;
};

// GML levels, loosest first: assignment; && || ^^; comparisons; & | ^; << >>;
// + -; * / div mod.
bool binop(const Tok &t, BinOp *op) {
  static const std::set<string> assign = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="};
  static const std::set<string> cmp = {"<", "<=", ">", ">=", "==", "!=", "<>"};
  auto set = [op](int prec, Op kind) { *op = BinOp{prec, kind}; return true; };
  const string &c = t.code;
  if (c == t.synt) {
    if (assign.count(c)) return set(1, Op::kAssign);
    if (c == "&&") return set(2, Op::kAnd);
    if (c == "||") return set(2, Op::kOr);
    if (cmp.count(c)) return set(3, Op::kCmp);
    if (c == "&" || c == "|" || c == "^") return set(4, Op::kBit);
    if (c == "<<" || c == ">>") return set(5, Op::kShift);
    if (c == "+" || c == "-") return set(6, Op::kArith);
    if (c == "*" || c == "/" || c == "%") return set(7, Op::kArith);
    return false;
  }
  switch (t.synt[0]) {  // word operators
    case '&': return set(2, Op::kAnd);
    case '|': return set(2, Op::kOr);
    case '^': return set(2, Op::kXor);  // xor, and ^^ as log_xor
    case '@': return set(7, Op::kArith);  // div, mod
  }
  return false;
}

bool prefix(const Tok &t) {
  return punct(t, "!") || punct(t, "~") || punct(t, "-") || punct(t, "+") || punct(t, "++") ||
         punct(t, "--") || (t.synt[0] == '!' && t.code != t.synt);  // not
}

struct Node {
  enum Kind { kLeaf, kParen, kCall, kIndex, kMember, kUnary, kPostfix, kBinary, kCast } kind;
  vector<Tok> toks;  // leaf text, or the operator and brackets
  vector<unique_ptr<Node>> kids;
  BinOp op{};
};
using PNode = unique_ptr<Node>;

PNode make(Node::Kind k) {
  PNode n(new Node);
  n->kind = k;
  return n;
}

struct Parser {
  const vector<Tok> &t;
  size_t i;

  const Tok *peek() {
    while (i < t.size() && space(t[i])) i++;
    return i < t.size() ? &t[i] : nullptr;
  }

  // Comma-separated expressions up to the closer, which is consumed.
  bool list(Node &n, const std::function<bool(const Tok &)> &closer) {
    const Tok *k = peek();
    if (k && closer(*k)) return n.toks.push_back(t[i++]), true;
    for (;;) {
      PNode e = expr(1);
      if (!e) return false;
      n.kids.push_back(std::move(e));
      k = peek();
      if (!k) return false;
      if (closer(*k)) return n.toks.push_back(t[i++]), true;
      if (!punct(*k, ",")) return false;
      i++;
    }
  }

  PNode primary() {
    const Tok *k = peek();
    if (!k) return nullptr;
    if (punct(*k, "(")) {
      PNode n = make(Node::kParen);
      n->toks.push_back(t[i++]);
      return list(*n, [](const Tok &c) { return punct(c, ")"); }) ? std::move(n) : nullptr;
    }
    PNode n = make(Node::kLeaf);
    if (scope(*k)) {
      n->toks.push_back(t[i++]);
      k = peek();
      if (!k || !name(*k)) return nullptr;
    }
    if (name(*k) || number(*k) || str(*k)) {
      n->toks.push_back(t[i++]);
      return n;
    }
    if (k->synt[0] == 't' && i + 1 < t.size() && punct(t[i + 1], "(")) {  // cast: double(x)
      n->toks.push_back(t[i++]);
      return n;
    }
    return nullptr;
  }

  PNode postfix(PNode n) {
    for (const Tok *k; n && (k = peek());) {
      if (punct(*k, "(") || open2d(*k)) {
        const bool twod = open2d(*k);
        PNode c = make(Node::kCall);
        c->kids.push_back(std::move(n));
        c->toks.push_back(t[i++]);
        const bool ok = twod ? list(*c, close2d) : list(*c, [](const Tok &x) { return punct(x, ")"); });
        if (!ok) return nullptr;
        n = std::move(c);
      } else if (punct(*k, "[")) {
        PNode x = make(Node::kIndex);
        x->kids.push_back(std::move(n));
        x->toks.push_back(t[i++]);
        if (!list(*x, [](const Tok &c) { return punct(c, "]"); }) || x->kids.size() != 2) return nullptr;
        n = std::move(x);
      } else if (punct(*k, ".") || punct(*k, "->")) {
        PNode m = make(Node::kMember);
        m->kids.push_back(std::move(n));
        m->toks.push_back(t[i++]);
        const Tok *f = peek();
        if (!f || !name(*f)) return nullptr;
        m->toks.push_back(t[i++]);
        n = std::move(m);
      } else if (punct(*k, "++") || punct(*k, "--")) {
        PNode p = make(Node::kPostfix);
        p->kids.push_back(std::move(n));
        p->toks.push_back(t[i++]);
        n = std::move(p);
      } else {
        break;
      }
    }
    return n;
  }

  // (double) x: the parser's casts, as for real division.
  bool cast_ahead() {
    size_t j = i + 1;
    while (j < t.size() && space(t[j])) j++;
    if (j >= t.size() || (t[j].synt[0] != 't' && t[j].synt[0] != 'c')) return false;
    for (j++; j < t.size() && space(t[j]); j++) {}
    return j < t.size() && punct(t[j], ")");
  }

  PNode unary() {
    const Tok *k = peek();
    if (k && (cast(*k) || (punct(*k, "(") && cast_ahead()))) {
      PNode c = make(Node::kCast);
      for (int n = cast(*k) ? 1 : 3; n > 0; n--) c->toks.push_back(t[i++]), peek();
      PNode x = unary();
      if (!x) return nullptr;
      c->kids.push_back(std::move(x));
      return c;
    }
    if (k && prefix(*k)) {
      PNode u = make(Node::kUnary);
      u->toks.push_back(t[i++]);
      PNode x = unary();
      if (!x) return nullptr;
      u->kids.push_back(std::move(x));
      return u;
    }
    return postfix(primary());
  }

  PNode expr(int min_prec) {
    PNode left = unary();
    BinOp op;
    for (const Tok *k; left && (k = peek()) && binop(*k, &op) && op.prec >= min_prec;) {
      PNode b = make(Node::kBinary);
      b->op = op;
      b->toks.push_back(t[i++]);
      PNode right = expr(op.kind == Op::kAssign ? op.prec : op.prec + 1);
      if (!right) return nullptr;
      b->kids.push_back(std::move(left));
      b->kids.push_back(std::move(right));
      left = std::move(b);
    }
    return left;
  }
};

string leaf_code(const Node &n) {
  string s;
  for (const Tok &k : n.toks) s += k.code;
  return s;
}

struct Printer {
  bool gm8;

  // Accessors and casts the parser generates read, they don't act.
  static bool pure_callee(const Node &callee) {
    if (callee.kind != Node::kLeaf) return false;
    if (callee.toks.back().synt[0] == 'c' || callee.toks.back().synt[0] == 't') return true;
    const string c = leaf_code(callee);
    return c.compare(0, 18, "enigma::varaccess_") == 0 || c.compare(0, 16, "enigma::glaccess") == 0 ||
           c == "enigma::varargs";
  }

  static bool fx(const Node &n) {
    switch (n.kind) {
      case Node::kCall:
        if (!pure_callee(*n.kids[0]) && !open2d(n.toks[0])) return true;
        break;
      case Node::kPostfix: return true;
      case Node::kUnary:
        if (punct(n.toks[0], "++") || punct(n.toks[0], "--")) return true;
        break;
      case Node::kBinary:
        if (n.op.kind == Op::kAssign) return true;
        break;
      default: break;
    }
    for (const PNode &k : n.kids)
      if (fx(*k)) return true;
    return false;
  }

  // A variable read that another operand could change.
  static bool read(const Node &n) {
    switch (n.kind) {
      case Node::kLeaf: return name(n.toks.back());
      case Node::kIndex: case Node::kMember: return read(*n.kids[0]) && !fx(n);
      case Node::kCall: return (pure_callee(*n.kids[0]) || open2d(n.toks[0])) && !fx(n);
      default: return false;
    }
  }

  // (enigma::varargs(), a, b): the overloaded comma returns a reference to
  // the temporary pack, so auto&& would dangle; bind it by value.
  static bool varargs_pack(const Node &n) {
    return n.kind == Node::kParen && n.kids.size() > 1 && n.kids[0]->kind == Node::kCall &&
           n.kids[0]->kids[0]->kind == Node::kLeaf && leaf_code(*n.kids[0]->kids[0]) == "enigma::varargs";
  }

  static bool order_matters(const Node &a, const Node &b) {
    return (fx(a) && (fx(b) || read(b))) || (read(a) && fx(b));
  }

  static bool literal(const Node &n) { return n.kind == Node::kLeaf && !name(n.toks.back()); }
  static bool string_literal(const Node &n) { return n.kind == Node::kLeaf && str(n.toks.back()); }

  Out wrap(const Node &n) {
    if (n.kind != Node::kBinary) return print(n);
    return Out().add("(").add(print(n)).add(")");
  }

  Out truth(const Node &n) {
    return Out().add(gm8 ? "enigma::gml_truth(" : "bool(").add(print(n)).add(")");
  }

  // Evaluates `left` first, bound to enigma_lhs, then `build` with it.
  Out ordered(const Node &left, const Node &right, const std::function<Out(Out)> &build) {
    if (!order_matters(left, right)) return build(wrap(left));
    const bool copy = read(left);
    Out o;
    o.add("[&]()->decltype(auto){").add(copy ? "auto enigma_lhs=" : "auto&& enigma_lhs=").add(print(left));
    o.add(";return ").add(build(Out().add(copy ? "enigma_lhs" : "std::forward<decltype(enigma_lhs)>(enigma_lhs)")));
    return o.add(";}()");
  }

  static const char *assign_hook(const Node &target, bool gm8) {
    static const std::set<string> int_views = {
        "view_xview", "view_yview", "view_wview", "view_hview", "view_xport", "view_yport",
        "view_wport", "view_hport", "view_hborder", "view_vborder", "view_hspeed", "view_vspeed"};
    const Node *root = target.kind == Node::kIndex || target.kind == Node::kCall ? target.kids[0].get() : &target;
    if (root->kind != Node::kLeaf) return nullptr;
    const string c = leaf_code(*root);
    if (c == "background_index") return "enigma::gml_background_index_assigned(";
    if (gm8 && int_views.count(c)) return "enigma::gml_round_view(";  // GM8 stores these as integers
    return nullptr;
  }

  Out binary(const Node &n) {
    const Node &l = *n.kids[0], &r = *n.kids[1];
    const Tok &op = n.toks[0];
    switch (n.op.kind) {
      case Op::kAssign: {
        // GML evaluates an assignment's target (its index) before the value.
        const Out body = fx(l) && !literal(r) ? ordered(l, r, [&](Out lhs) { return lhs.add(op).add(print(r)); })
                                              : Out().add(print(l)).add(op).add(print(r));
        const char *hook = assign_hook(l, gm8);
        return hook ? Out().add(hook).add(body).add(")") : body;
      }
      case Op::kAnd: case Op::kOr: case Op::kXor:
        // GM8 computes both operands, left first, even when the left one decides.
        if (gm8)
          return Out().add("enigma::gml_both{").add(truth(l)).add(",").add(truth(r))
              .add(n.op.kind == Op::kAnd ? "}.all()" : n.op.kind == Op::kOr ? "}.any()" : "}.one()");
        if (n.op.kind != Op::kXor) return Out().add(wrap(l)).add(n.op.kind == Op::kAnd ? "&&" : "||").add(wrap(r));
        return ordered(l, r, [&](Out lhs) { return Out().add("(bool(").add(lhs).add(")!=").add(truth(r)).add(")"); });
      case Op::kBit: case Op::kShift: {
        const string c = op.code;
        const char *fn = c == "&" ? "enigma::gml_bitand(" : c == "|" ? "enigma::gml_bitor(" : c == "^" ? "enigma::gml_bitxor("
                         : c == "<<" ? "enigma::gml_shl(" : "enigma::gml_shr(";
        return ordered(l, r, [&](Out lhs) { return Out().add(fn).add(lhs).add(",").add(print(r)).add(")"); });
      }
      default: {
        // "a" + "b" would add two pointers and "B" < "a" compare them.
        if ((op.code == "+" || n.op.kind == Op::kCmp) && string_literal(l) && string_literal(r))
          return Out().add("std::string{").add(print(l)).add("}").add(op.code == "<>" ? Tok{"!=", "!="} : op).add(print(r));
        const Tok o = op.code == "<>" ? Tok{"!=", "!="} : op;
        const bool word = o.code != o.synt;  // div, mod: keep them apart from the operands
        return ordered(l, r, [&](Out lhs) {
          Out out = lhs;
          if (word) out.add(" ");
          out.add(o);
          if (word) out.add(" ");
          return out.add(wrap(r));
        });
      }
    }
  }

  Out call(const Node &n) {
    const Node &callee = *n.kids[0];
    const bool twod = open2d(n.toks[0]);
    Out head;
    if (callee.kind == Node::kLeaf && leaf_code(callee) == "round") head.add("enigma::gml_round");
    else head.add(print(callee));
    auto arg = [&](const Out &a) {
      return twod && gm8 ? Out().add("enigma::gml_index(").add(a).add(")") : a;
    };
    bool order = false;
    for (size_t a = 1; a < n.kids.size() && !order; a++)
      for (size_t b = a + 1; b < n.kids.size() && !order; b++)
        order = order_matters(*n.kids[a], *n.kids[b]);
    Out o;
    if (!order) {
      o.add(head).add(n.toks[0]);
      for (size_t a = 1; a < n.kids.size(); a++) {
        if (a > 1) o.add(",");
        o.add(arg(print(*n.kids[a])));
      }
      return o.add(n.toks.back());
    }
    // GML evaluates arguments left to right; C++ doesn't say.
    o.add("[&]()->decltype(auto){");
    Out args;
    for (size_t a = 1; a < n.kids.size(); a++) {
      const string v = "enigma_arg" + std::to_string(a - 1);
      const bool copy = read(*n.kids[a]) || varargs_pack(*n.kids[a]);
      o.add(copy ? "auto " : "auto&& ").add(v + "=").add(print(*n.kids[a])).add(";");
      if (a > 1) args.add(",");
      args.add(arg(Out().add(copy ? v : "std::forward<decltype(" + v + ")>(" + v + ")")));
    }
    return o.add("return ").add(head).add(n.toks[0]).add(args).add(n.toks.back()).add(";}()");
  }

  Out index(const Node &n) {
    Out o = print(*n.kids[0]);
    o.add(n.toks[0]);
    const Node &i = *n.kids[1];
    if (!gm8) o.add(print(i));
    else if (i.kind == Node::kCall && i.kids.size() == 2 && i.kids[0]->kind == Node::kLeaf &&
             leaf_code(*i.kids[0]) == "int")  // the parser's int(...) around an index
      o.add("enigma::gml_index(").add(print(*i.kids[1])).add(")");
    else
      o.add("enigma::gml_index(").add(print(i)).add(")");
    return o.add(n.toks.back());
  }

  Out print(const Node &n) {
    Out o;
    switch (n.kind) {
      case Node::kLeaf:
        for (const Tok &k : n.toks) o.add(k);
        return o;
      case Node::kParen:
        o.add(n.toks[0]);
        for (size_t k = 0; k < n.kids.size(); k++) {
          if (k) o.add(",");
          o.add(print(*n.kids[k]));
        }
        return o.add(n.toks.back());
      case Node::kCall: return call(n);
      case Node::kIndex: return index(n);
      case Node::kMember: return o.add(print(*n.kids[0])).add(n.toks[0]).add(n.toks[1]);
      case Node::kPostfix: return o.add(wrap(*n.kids[0])).add(n.toks[0]);
      case Node::kUnary: {
        const Tok &op = n.toks[0];
        const Node &x = *n.kids[0];
        if (punct(op, "!") || op.synt[0] == '!')
          return gm8 ? o.add("!enigma::gml_truth(").add(print(x)).add(")") : o.add("!").add(wrap(x));
        if (punct(op, "~")) return o.add("enigma::gml_bitnot(").add(print(x)).add(")");
        return o.add(op).add(wrap(x));
      }
      case Node::kBinary: return binary(n);
      case Node::kCast:
        for (const Tok &k : n.toks) o.add(k);
        return o.add(wrap(*n.kids[0]));
    }
    return o;
  }

  // A condition: GM8 counts a real as true from 0.5.
  Out condition(const Node &paren) {
    if (!gm8 || paren.kind != Node::kParen || paren.kids.size() != 1) return print(paren);
    return Out().add(paren.toks[0]).add(truth(*paren.kids[0])).add(paren.toks.back());
  }
};

bool starts_expression(const vector<Tok> &t, size_t i) {
  const Tok &k = t[i];
  return name(k) || number(k) || str(k) || punct(k, "(") || scope(k) || prefix(k) || cast(k) ||
         (k.synt[0] == 't' && i + 1 < t.size() && punct(t[i + 1], "("));
}

vector<string> failures;

}  // namespace

const vector<string> &gml_expression_failures() { return failures; }
void gml_expression_failures_clear() { failures.clear(); }

void gml_expressions(string &code, string &synt) {
  if (code.size() != synt.size()) return;
  const vector<Tok> t = tokenize(code, synt);
  Printer pr{setting::compliance_mode <= setting::COMPL_GM8};
  Out out;
  string keyword;           // the control keyword before a parenthesized condition
  int depth = 0;            // parentheses we copy rather than parse (for loops)
  int for_depth = -1, for_part = 0;

  // Copies an expression we can't parse, up to the end of its statement, and
  // records it: printed as is it would keep C++ semantics.
  auto verbatim = [&](size_t i) {
    string text;
    for (int d = 0; i < t.size(); i++) {
      const string &c = t[i].code;
      if (d == 0 && (punct(t[i], ";") || punct(t[i], "{") || punct(t[i], "}"))) break;
      if (c == "(" || c == "[") d++;
      if (c == ")" || c == "]") { if (d == 0) break; d--; }
      out.add(t[i]);
      text += t[i].code;
    }
    failures.push_back(text);
    return i;
  };

  for (size_t i = 0; i < t.size();) {
    const Tok &k = t[i];
    const char s = k.synt[0];
    if (s == 's' || s == 'f') {
      keyword = k.code;
      while (!keyword.empty() && keyword.back() == ' ') keyword.pop_back();
      out.add(k), i++;
      if (s == 'f') {
        while (i < t.size() && space(t[i])) out.add(t[i++]);
        if (i < t.size() && punct(t[i], "(")) out.add(t[i++]), for_depth = ++depth, for_part = 0;
        keyword.clear();
      }
      continue;
    }
    if (!keyword.empty() && punct(k, "(")) {  // if (...), while (...), switch (...)
      Parser p{t, i};
      PNode cond = p.primary();
      if (cond) {
        const bool truth = keyword == "if" || keyword == "while" || keyword == "until";
        out.add(truth ? pr.condition(*cond) : pr.print(*cond));
        i = p.i;
      } else {
        i = verbatim(i);
      }
      keyword.clear();
      continue;
    }
    if (!space(k)) keyword.clear();
    if (starts_expression(t, i)) {
      Parser p{t, i};
      PNode e = p.expr(1);
      if (e) {
        const bool cond = depth == for_depth && for_part == 1;
        if (cond && pr.gm8) out.add(pr.truth(*e));
        else out.add(pr.print(*e));
        i = p.i;
      } else {
        i = verbatim(i);
      }
      continue;
    }
    if (depth == for_depth && punct(k, ";")) for_part++;
    if (punct(k, "(")) depth++;
    if (punct(k, ")")) {
      if (depth == for_depth) for_depth = -1;
      depth--;
    }
    out.add(k), i++;
  }
  code = out.code, synt = out.synt;
}
