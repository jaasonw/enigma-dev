// Tests for the print-time GML expression pass (CompilerSource/parser/gml_expr.cpp).
// Inputs are the legacy parser's final code/synt pairs; Synt() builds the synt
// string the parser would give simple code.
#include "parser/gml_expr.h"
#include "settings.h"

#include <gtest/gtest.h>

#include <cctype>
#include <map>
#include <string>

namespace {

// Tags: names n, numbers 0, strings ", casts c ("int", "(double)"), control
// keywords s/f/p, word operators with their operator's character.
std::string Synt(const std::string &code) {
  static const std::map<std::string, char> words = {
      {"and", '&'}, {"or", '|'}, {"xor", '^'}, {"log_xor", '^'}, {"not", '!'}, {"div", '@'}, {"mod", '@'},
      {"int", 'c'}, {"if", 's'}, {"while", 's'}, {"until", 's'}, {"switch", 's'}, {"with", 's'},
      {"repeat", 's'}, {"for", 'f'}, {"return", 'p'}, {"var", 't'}};
  std::string s;
  for (size_t i = 0; i < code.size();) {
    if (code.compare(i, 8, "(double)") == 0) { s += "cccccccc"; i += 8; continue; }
    const unsigned char c = code[i];
    if (isalpha(c) || c == '_' || c == '$') {
      size_t j = i;
      while (j < code.size() && (isalnum((unsigned char) code[j]) || code[j] == '_' || code[j] == ':' || code[j] == '$')) j++;
      const std::string w = code.substr(i, j - i);
      const auto it = words.find(w);
      s += std::string(j - i, it == words.end() ? 'n' : it->second);
      i = j;
    } else if (isdigit(c)) {
      size_t j = i;
      while (j < code.size() && (isdigit((unsigned char) code[j]) || code[j] == '.')) j++;
      s += std::string(j - i, '0');
      i = j;
    } else {
      s += code[i++];
    }
  }
  return s;
}

std::string Run(std::string code, bool gm8) {
  setting::compliance_mode = gm8 ? setting::COMPL_GM8 : setting::COMPL_STANDARD;
  std::string synt = Synt(code);
  gml_expression_failures_clear();
  gml_expressions(code, synt);
  EXPECT_EQ(code.size(), synt.size());
  return code;
}

std::string Gm8(const std::string &code) { return Run(code, true); }
std::string Std(const std::string &code) { return Run(code, false); }

}  // namespace

TEST(GmlExpr, ConditionsUseGm8Truth) {
  EXPECT_EQ(Gm8("if(a)b=1;"), "if(enigma::gml_truth(a))b=1;");
  EXPECT_EQ(Gm8("while(a)b=1;"), "while(enigma::gml_truth(a))b=1;");
  EXPECT_EQ(Gm8("for(i=0;i<n;i+=1)b=1;"), "for(i=0;enigma::gml_truth(i<n);i+=1)b=1;");
  EXPECT_EQ(Std("if(a)b=1;"), "if(a)b=1;");
}

TEST(GmlExpr, LogicalOperators) {
  EXPECT_EQ(Gm8("x=!a;"), "x=!enigma::gml_truth(a);");
  // GM8 computes both operands, left first.
  EXPECT_EQ(Gm8("x=a and b;"), "x=enigma::gml_both{enigma::gml_truth(a),enigma::gml_truth(b)}.all();");
  EXPECT_EQ(Gm8("x=a||b;"), "x=enigma::gml_both{enigma::gml_truth(a),enigma::gml_truth(b)}.any();");
  EXPECT_EQ(Gm8("x=a log_xor b;"), "x=enigma::gml_both{enigma::gml_truth(a),enigma::gml_truth(b)}.one();");
  // && || ^^ share one level, left to right.
  EXPECT_EQ(Std("x=1||0&&0;"), "x=(1||0)&&0;");
  EXPECT_EQ(Std("x=1 log_xor 1&&0;"), "x=((bool(1)!=bool(1)))&&0;");
}

TEST(GmlExpr, ComparisonGroupsBelowLogic) {
  // The parser already made value-position = into ==; GML groups it tighter than and.
  EXPECT_EQ(Std("if(a<3&&b==-2&&!c)d=1;"), "if(((a<3)&&(b==-2))&&!c)d=1;");
}

TEST(GmlExpr, BitwiseAndRound) {
  EXPECT_EQ(Std("x=a&1;"), "x=enigma::gml_bitand(a,1);");
  EXPECT_EQ(Std("x=1<<40;"), "x=enigma::gml_shl(1,40);");
  EXPECT_EQ(Std("x=~a;"), "x=enigma::gml_bitnot(a);");
  EXPECT_EQ(Std("x=round(a);"), "x=enigma::gml_round(a);");
}

TEST(GmlExpr, StringLiteralPairs) {
  // Each string literal is one '"' in the parser's code.
  EXPECT_EQ(Std("x=\"<\";"), "x=std::string{\"}<\";");
  EXPECT_EQ(Std("x=\"+\";"), "x=std::string{\"}+\";");
}

TEST(GmlExpr, Indices) {
  EXPECT_EQ(Gm8("x=a[int(i)];"), "x=a[enigma::gml_index(i)];");
  EXPECT_EQ(Std("x=a[int(i)];"), "x=a[int(i)];");
  // a[i, j], which the parser prints as a(i, j) but tags as brackets.
  std::string code = "x=a(i,j);", synt = "n=n[n,n];";
  setting::compliance_mode = setting::COMPL_GM8;
  gml_expressions(code, synt);
  EXPECT_EQ(code, "x=a(enigma::gml_index(i),enigma::gml_index(j));");
}

TEST(GmlExpr, EvaluationOrder) {
  EXPECT_EQ(Std("x=f(a,g());"),
            "x=[&]()->decltype(auto){auto enigma_arg0=a;auto&& enigma_arg1=g();"
            "return f(enigma_arg0,std::forward<decltype(enigma_arg1)>(enigma_arg1));}();");
  EXPECT_EQ(Std("x=a+f();"), "x=[&]()->decltype(auto){auto enigma_lhs=a;return enigma_lhs+f();}();");
  EXPECT_EQ(Std("a[int(f())]=g();"),
            "[&]()->decltype(auto){auto&& enigma_lhs=a[int(f())];"
            "return std::forward<decltype(enigma_lhs)>(enigma_lhs)=g();}();");
  // Reads with nothing to reorder stay as they are.
  EXPECT_EQ(Std("x=a+b;"), "x=a+b;");
}

TEST(GmlExpr, GeneratedAccessorsAndCasts) {
  // Dot access and casts from the parser: reads, not side effects.
  EXPECT_EQ(Std("x=enigma::varaccess_hp(int(self))+enigma::varaccess_hp(int(other));"),
            "x=enigma::varaccess_hp(int(self))+enigma::varaccess_hp(int(other));");
  EXPECT_EQ(Std("x=a/(double)2;"), "x=a/(double)2;");
  EXPECT_EQ(Std("x=a+b/(double)c;"), "x=a+(b/(double)c);");
}

TEST(GmlExpr, AssignmentHooks) {
  EXPECT_EQ(Gm8("view_xview[int(0)]=x;"), "enigma::gml_round_view(view_xview[enigma::gml_index(0)]=x);");
  EXPECT_EQ(Std("view_xview[int(0)]=x;"), "view_xview[int(0)]=x;");
  EXPECT_EQ(Std("background_index[int(0)]=b;"),
            "enigma::gml_background_index_assigned(background_index[int(0)]=b);");
}

TEST(GmlExpr, LeftUnchanged) {
  EXPECT_EQ(Std("var a,b;"), "var a,b;");
  EXPECT_EQ(Std("x=c_white;"), "x=c_white;");
  EXPECT_EQ(Std("x=a?b:c;"), "x=a?b:c;");  // GML 8 has no ?:; C++'s passes through
  EXPECT_TRUE(gml_expression_failures().empty());
}

TEST(GmlExpr, UnparsedExpressionIsReported) {
  EXPECT_EQ(Std("x=a+;y=1;"), "x=a+;y=1;");
  ASSERT_EQ(gml_expression_failures().size(), 1u);
  EXPECT_EQ(gml_expression_failures()[0], "x=a+");
}
