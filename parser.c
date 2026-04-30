#include "parser.h"
#include <ctype.h>

/* ============================================================
 *  Assembly Parser — reads text file, encodes 32-bit binary,
 *  stores directly into memory[] (instruction segment).
 * ============================================================ */

/* ---- helpers ----------------------------------------------- */

static int parse_register(const char *s) {
    /* Accept R0..R31 or r0..r31 */
    if (s[0] == 'R' || s[0] == 'r') return atoi(s + 1);
    return atoi(s);
}

static int mnemonic_to_opcode(const char *m) {
    if (strcasecmp(m, "ADD")  == 0) return OP_ADD;
    if (strcasecmp(m, "SUB")  == 0) return OP_SUB;
    if (strcasecmp(m, "MULI") == 0) return OP_MULI;
    if (strcasecmp(m, "ADDI") == 0) return OP_ADDI;
    if (strcasecmp(m, "BNE")  == 0) return OP_BNE;
    if (strcasecmp(m, "ANDI") == 0) return OP_ANDI;
    if (strcasecmp(m, "XORI") == 0) return OP_XORI;
    if (strcasecmp(m, "J")    == 0) return OP_J;
    if (strcasecmp(m, "SLL")  == 0) return OP_SLL;
    if (strcasecmp(m, "SRL")  == 0) return OP_SRL;
    if (strcasecmp(m, "LW")   == 0) return OP_LW;
    if (strcasecmp(m, "SW")   == 0) return OP_SW;
    return -1;
}

/* ---- encoding ---------------------------------------------- */

/* R-Format: OPCODE(4) | R1(5) | R2(5) | R3(5) | SHAMT(13) = 32 */
static int32_t encode_r(int op, int r1, int r2, int r3, int shamt) {
    int32_t w = 0;
    w |= (op   & 0xF)    << 28;
    w |= (r1   & 0x1F)   << 23;
    w |= (r2   & 0x1F)   << 18;
    w |= (r3   & 0x1F)   << 13;
    w |= (shamt & 0x1FFF);
    return w;
}

/* I-Format: OPCODE(4) | R1(5) | R2(5) | IMMEDIATE(18) = 32 */
static int32_t encode_i(int op, int r1, int r2, int32_t imm) {
    int32_t w = 0;
    w |= (op  & 0xF)     << 28;
    w |= (r1  & 0x1F)    << 23;
    w |= (r2  & 0x1F)    << 18;
    w |= (imm & 0x3FFFF);          /* keep 18 bits */
    return w;
}

/* J-Format: OPCODE(4) | ADDRESS(28) = 32 */
static int32_t encode_j(int op, int32_t addr) {
    int32_t w = 0;
    w |= (op   & 0xF)       << 28;
    w |= (addr & 0x0FFFFFFF);
    return w;
}

/* ---- trim whitespace --------------------------------------- */

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

/* ---- public API -------------------------------------------- */

int parse_program(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "[PARSER] ERROR: Cannot open file '%s'\n", filename);
        return -1;
    }

    char line[512];
    int addr = 0;

    while (fgets(line, sizeof(line), fp) && addr <= INSTR_END) {
        char *trimmed = trim(line);

        /* skip blank lines and comments */
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';')
            continue;

        /* tokenise: mnemonic arg1 [arg2] [arg3] */
        char mnemonic[16] = {0};
        char a1[32] = {0}, a2[32] = {0}, a3[32] = {0};
        int fields = sscanf(trimmed, "%15s %31s %31s %31s", mnemonic, a1, a2, a3);

        if (fields < 1) continue;

        int opcode = mnemonic_to_opcode(mnemonic);
        if (opcode < 0) {
            fprintf(stderr, "[PARSER] WARNING: Unknown mnemonic '%s' at address %d\n",
                    mnemonic, addr);
            continue;
        }

        int fmt = get_format(opcode);
        int32_t encoded = 0;

        switch (fmt) {
        case FORMAT_R:
            if (opcode == OP_SLL || opcode == OP_SRL) {
                /* SLL R1 R2 SHAMT   (R3 forced to 0) */
                encoded = encode_r(opcode,
                                   parse_register(a1),
                                   parse_register(a2),
                                   0,
                                   atoi(a3));
            } else {
                /* ADD R1 R2 R3 */
                encoded = encode_r(opcode,
                                   parse_register(a1),
                                   parse_register(a2),
                                   parse_register(a3),
                                   0);
            }
            break;

        case FORMAT_I:
            /* ADDI R1 R2 IMM  /  BNE R1 R2 IMM  /  LW R1 R2 IMM  etc. */
            encoded = encode_i(opcode,
                               parse_register(a1),
                               parse_register(a2),
                               atoi(a3));
            break;

        case FORMAT_J:
            /* J ADDRESS */
            encoded = encode_j(opcode, atoi(a1));
            break;

        default:
            fprintf(stderr, "[PARSER] ERROR: Bad format for opcode %d\n", opcode);
            continue;
        }

        memory[addr] = encoded;
        printf("[PARSER] Addr %3d: %-6s %-4s %-4s %-4s  =>  0x%08X\n",
               addr, mnemonic, a1, a2, a3, (unsigned)encoded);
        addr++;
    }

    fclose(fp);
    instruction_count = addr;
    printf("[PARSER] Loaded %d instructions.\n\n", instruction_count);
    return instruction_count;
}
