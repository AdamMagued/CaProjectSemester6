#include "globals.h"

/* ============================================================
 *  Global variable DEFINITIONS (declared extern in globals.h)
 * ============================================================ */

/* Main memory: 2048 rows of 32-bit words
 * Indices 0-1023:    Instruction segment
 * Indices 1024-2047: Data segment                              */
int32_t memory[MEM_SIZE];

/* 32 General-Purpose Registers (R0 = 0 always) */
int32_t registers[NUM_REGISTERS];

/* Program Counter */
int32_t PC = 0;

/* Simulation clock */
int clock_cycle = 1;

/* Number of instructions loaded into memory */
int instruction_count = 0;

/* Flag: set to 1 when simulation should stop */
int program_done = 0;

/* Pipeline stage registers */
InstructionContext IF_stage;
InstructionContext ID_stage;
InstructionContext EX_stage;
InstructionContext MEM_stage;
InstructionContext WB_stage;

/* ---- Utility functions ------------------------------------- */

/* Return an empty/inactive pipeline context */
InstructionContext empty_context(void) {
    InstructionContext ctx;
    memset(&ctx, 0, sizeof(InstructionContext));
    ctx.is_active = 0;
    ctx.cycles_remaining = 0;
    ctx.branch_taken = 0;
    return ctx;
}

/* Convert opcode number to mnemonic string */
const char *opcode_to_string(int opcode) {
    switch (opcode) {
        case OP_ADD:  return "ADD";
        case OP_SUB:  return "SUB";
        case OP_MULI: return "MULI";
        case OP_ADDI: return "ADDI";
        case OP_BNE:  return "BNE";
        case OP_ANDI: return "ANDI";
        case OP_XORI: return "XORI";
        case OP_J:    return "J";
        case OP_SLL:  return "SLL";
        case OP_SRL:  return "SRL";
        case OP_LW:   return "LW";
        case OP_SW:   return "SW";
        default:      return "???";
    }
    return "???";
}

/* Determine instruction format from opcode */
int get_format(int opcode) {
    switch (opcode) {
        case OP_ADD:
        case OP_SUB:
        case OP_SLL:
        case OP_SRL:
            return FORMAT_R;
        case OP_MULI:
        case OP_ADDI:
        case OP_BNE:
        case OP_ANDI:
        case OP_XORI:
        case OP_LW:
        case OP_SW:
            return FORMAT_I;
        case OP_J:
            return FORMAT_J;
        default:
            return -1;
    }
}
