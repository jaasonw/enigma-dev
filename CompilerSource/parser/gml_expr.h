#ifndef ENIGMA_PARSER_GML_EXPR_H
#define ENIGMA_PARSER_GML_EXPR_H

#include <string>

// Re-parses the parser's final code/synt pair with GML precedence and prints
// each expression back with GML semantics: left-to-right evaluation, real
// bitwise operands, string literal comparison, and under GM8 compliance the
// 0.5 truth threshold and rounded indices. Expressions it can't parse are
// left as they are.
void gml_expressions(std::string &code, std::string &synt);

#endif
