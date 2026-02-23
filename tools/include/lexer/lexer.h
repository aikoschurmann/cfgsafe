#ifndef __LEXER_H__
#define __LEXER_H__

typedef enum {
    TOKEN_IDENTIFIER,  // user, port, default, int, float, debug
    TOKEN_STRING_LIT,  // "42" 
    TOKEN_INT_LIT,     // 8080, 1, 65535
    TOKEN_FLOAT_LIT,   // 0.5, 0.0, 1.0

    TOKEN_KW_IMPORT,   // import
    TOKEN_KW_SCHEMA,   // schema
    TOKEN_KW_SECTION,  // section
    TOKEN_KW_TRUE,     // true
    TOKEN_KW_FALSE,    // false

    TOKEN_LBRACE,      // {
    TOKEN_RBRACE,      // }
    TOKEN_LBRACKET,    // [
    TOKEN_RBRACKET,    // ]
    TOKEN_LPAREN,      // (
    TOKEN_RPAREN,      // )
    TOKEN_COLON,       // :
    TOKEN_COMMA,       // ,
    TOKEN_DOT,         // . (used in validators.port_check)

    TOKEN_RANGE,       // .. (used in 1..65535)

    TOKEN_EOF,         // End of File
    TOKEN_INVALID      // For unrecognized characters
} TokenType;

#endif // __LEXER_H__