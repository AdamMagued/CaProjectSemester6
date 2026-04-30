#include "globals.h"
#include "parser.h"

/* ============================================================
 *  Temporary main() — tests the parser and verifies encoding.
 *  Will be replaced with the full pipeline loop later.
 * ============================================================ */

/* Print a 32-bit value in binary for visual verification */
static void print_binary(int32_t val) {
    for (int i = 31; i >= 0; i--) {
        printf("%d", (val >> i) & 1);
        if (i == 28 || i == 23 || i == 18 || i == 13)
            printf(" ");
    }
}

/* Decode and display an instruction from memory to verify correctness */
static void verify_instruction(int addr) {
    int32_t raw = memory[addr];
    int opcode = (raw >> 28) & 0xF;
    int fmt    = get_format(opcode);

    printf("  [%3d] 0x%08X  bin=", addr, (unsigned)raw);
    print_binary(raw);
    printf("\n");
    printf("        opcode=%d (%s)  ", opcode, opcode_to_string(opcode));

    switch (fmt) {
    case FORMAT_R: {
        int r1    = (raw >> 23) & 0x1F;
        int r2    = (raw >> 18) & 0x1F;
        int r3    = (raw >> 13) & 0x1F;
        int shamt = raw & 0x1FFF;
        printf("R-type  R1=R%d  R2=R%d  R3=R%d  SHAMT=%d\n", r1, r2, r3, shamt);
        break;
    }
    case FORMAT_I: {
        int r1 = (raw >> 23) & 0x1F;
        int r2 = (raw >> 18) & 0x1F;
        int32_t imm = raw & 0x3FFFF;
        /* Sign-extend 18-bit immediate */
        if (imm & 0x20000) imm |= 0xFFFC0000;
        printf("I-type  R1=R%d  R2=R%d  IMM=%d\n", r1, r2, imm);
        break;
    }
    case FORMAT_J: {
        int32_t address = raw & 0x0FFFFFFF;
        printf("J-type  ADDRESS=%d\n", address);
        break;
    }
    default:
        printf("UNKNOWN FORMAT\n");
    }
}

int main(int argc, char *argv[]) {
    const char *filename = "program.txt";
    if (argc > 1) filename = argv[1];

    /* Clear hardware state */
    memset(memory, 0, sizeof(memory));
    memset(registers, 0, sizeof(registers));
    PC = 0;

    printf("========================================\n");
    printf("  Package 1 — Parser Test\n");
    printf("========================================\n\n");

    int count = parse_program(filename);
    if (count < 0) {
        fprintf(stderr, "Failed to parse program.\n");
        return 1;
    }

    printf("========================================\n");
    printf("  Verification: Decoding from memory\n");
    printf("========================================\n\n");

    for (int i = 0; i < count; i++) {
        verify_instruction(i);
        printf("\n");
    }

    printf("Parser test complete. %d instructions in memory.\n", count);
    return 0;
}
