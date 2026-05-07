#include "parser.h"
#include <ctype.h>

/* ============================================================
 *  Assembly Parser — reads text file, encodes to 32-bit binary,
 *  stores into memory[] (instruction segment 0–1023).
 * ============================================================ */

/* Lookup table: mnemonic string → opcode */
static const struct { const char *name; int opcode; } MNEMONICS[] = {
    {"ADD", OP_ADD}, {"SUB", OP_SUB}, {"MULI", OP_MULI}, {"ADDI", OP_ADDI},
    {"BNE", OP_BNE}, {"ANDI", OP_ANDI}, {"XORI", OP_XORI}, {"J", OP_J},
    {"SLL", OP_SLL}, {"SRL", OP_SRL}, {"LW", OP_LW}, {"SW", OP_SW},
    {NULL, -1}
};

/* Parse "R5" or "r5" → 5 */
static int reg(const char *s) {
    return (s[0] == 'R' || s[0] == 'r') ? atoi(s + 1) : atoi(s);
}

/* Mnemonic → opcode (-1 if unknown) */
static int opcode_of(const char *m) {
    for (int i = 0; MNEMONICS[i].name; i++)
        if (strcasecmp(m, MNEMONICS[i].name) == 0) return MNEMONICS[i].opcode;
    return -1;
}

/* Encode any instruction into a single 32-bit word */
static int32_t encode(int op, int r1, int r2, int r3, int shamt, int32_t imm, int32_t addr) {
    int fmt = get_format(op);
    int32_t w = (op & 0xF) << 28;
    if (fmt == FORMAT_R) {
        w |= (r1 & 0x1F) << 23 | (r2 & 0x1F) << 18 | (r3 & 0x1F) << 13 | (shamt & 0x1FFF);
    } else if (fmt == FORMAT_I) {
        w |= (r1 & 0x1F) << 23 | (r2 & 0x1F) << 18 | (imm & 0x3FFFF);
    } else { /* FORMAT_J */
        w |= (addr & 0x0FFFFFFF);
    }
    return w;
}

/* Trim leading/trailing whitespace in-place */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

/* ---- Public API -------------------------------------------- */
int parse_program(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) { fprintf(stderr, "[PARSER] Cannot open '%s'\n", filename); return -1; }

    char line[512];
    int addr = 0;

    while (fgets(line, sizeof(line), fp) && addr <= INSTR_END) {
        char *t = trim(line);
        if (!*t || *t == '#' || *t == ';') continue;  /* skip blanks/comments */

        char mn[16]={0}, a1[32]={0}, a2[32]={0}, a3[32]={0};
        if (sscanf(t, "%15s %31s %31s %31s", mn, a1, a2, a3) < 1) continue;

        int op = opcode_of(mn);
        if (op < 0) { fprintf(stderr, "[PARSER] Unknown: '%s' at %d\n", mn, addr); continue; }

        int32_t encoded;
        int fmt = get_format(op);
        if (fmt == FORMAT_R) {
            /* SLL/SRL: R1 R2 SHAMT  |  ADD/SUB: R1 R2 R3 */
            int is_shift = (op == OP_SLL || op == OP_SRL);
            encoded = encode(op, reg(a1), reg(a2),
                             is_shift ? 0 : reg(a3),
                             is_shift ? atoi(a3) : 0, 0, 0);
        } else if (fmt == FORMAT_I) {
            encoded = encode(op, reg(a1), reg(a2), 0, 0, atoi(a3), 0);
        } else {
            encoded = encode(op, 0, 0, 0, 0, 0, atoi(a1));
        }

        memory[addr] = encoded;
        printf("[PARSER] Addr %3d: %-6s %-4s %-4s %-4s  =>  0x%08X\n",
               addr, mn, a1, a2, a3, (unsigned)encoded);
        addr++;
    }

    fclose(fp);
    instruction_count = addr;
    printf("[PARSER] Loaded %d instructions.\n\n", instruction_count);
    return instruction_count;
}
