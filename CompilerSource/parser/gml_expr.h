#ifndef ENIGMA_PARSER_GML_EXPR_H
#define ENIGMA_PARSER_GML_EXPR_H

#include <string>
#include <vector>

// Re-parses the parser's final code/synt pair with GML precedence and prints
// each expression back with GML semantics: left-to-right evaluation, real
// bitwise operands, string literal comparison, and under GM8 compliance the
// 0.5 truth threshold and rounded indices. An expression it can't parse is
// copied unchanged and recorded; the compiler fails the build on any.
void gml_expressions(std::string &code, std::string &synt);

// Expressions gml_expressions couldn't parse since the last clear, as code text.
const std::vector<std::string> &gml_expression_failures();
void gml_expression_failures_clear();

#endif
