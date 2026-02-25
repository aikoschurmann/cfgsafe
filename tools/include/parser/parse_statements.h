#pragma once

#include "ast.h"
#include "parser.h"

AstNode *parse_program(Parser *p, ParseError *err);
