#ifndef PARSER_H
#define PARSER_H

#include "globals.h"

/* Parse an assembly file and load binary instructions into memory[0..N-1].
 * Returns the number of instructions loaded. */
int parse_program(const char *filename);

#endif /* PARSER_H */
