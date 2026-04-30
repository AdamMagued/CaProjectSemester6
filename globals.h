#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 *  PACKAGE 1: Spicy Von Neumann Fillet with extra shifts
 *  Von Neumann | 2048x32 memory | 5-stage pipeline
 * ============================================================ */

/* ---- Memory ------------------------------------------------ */
#define MEM_SIZE          2048
#define INSTR_START       0
#define INSTR_END         1023
#define DATA_START        1024
#define DATA_END          2047

/* ---- Registers --------------------------------------------- */
#define NUM_REGISTERS     32
#define ZERO_REG          0       /* R0 is hard-wired to 0 */

/* ---- Instruction Opcodes ----------------------------------- */
#define OP_ADD   0
#define OP_SUB   1
#define OP_MULI  2
#define OP_ADDI  3
#define OP_BNE   4
#define OP_ANDI  5
#define OP_XORI  6
#define OP_J     7
#define OP_SLL   8
#define OP_SRL   9
#define OP_LW   10
#define OP_SW   11
#define NUM_OPCODES 12

/* ---- Instruction format helpers ---------------------------- */
/* R-Format: OPCODE(4) | R1(5) | R2(5) | R3(5) | SHAMT(13)    */
/* I-Format: OPCODE(4) | R1(5) | R2(5) | IMMEDIATE(18)         */
/* J-Format: OPCODE(4) | ADDRESS(28)                            */

#define OPCODE_BITS   4
#define R1_BITS       5
#define R2_BITS       5
#define R3_BITS       5
#define SHAMT_BITS   13
#define IMM_BITS     18
#define ADDR_BITS    28

/* ---- Pipeline stage "baton" -------------------------------- */
typedef struct {
    int is_active;               /* 1 = instruction present, 0 = empty/flushed */
    int cycles_remaining;        /* for stages that take 2 clock cycles (ID, EX) */

    /* Populated during FETCH */
    int32_t instruction_address; /* PC value when fetched */
    int32_t raw_instruction;     /* raw 32-bit word from memory */

    /* Populated during DECODE (all possible fields parsed) */
    int opcode;
    int r1;                      /* destination (usually) */
    int r2;                      /* source 1 */
    int r3;                      /* source 2 (R-type) */
    int shamt;                   /* shift amount */
    int32_t imm;                 /* signed immediate (I-type) */
    int32_t address;             /* jump address (J-type) */

    /* Register values read during Decode */
    int32_t val_r1;              /* value of R1 (needed for BNE, SW) */
    int32_t val_r2;              /* value of R2 */
    int32_t val_r3;              /* value of R3 */

    /* Populated during EXECUTE */
    int32_t alu_result;
    int branch_taken;            /* 1 if branch/jump condition met */
    int32_t branch_target;       /* target address if branch taken */

    /* Populated during MEMORY */
    int32_t mem_read_data;       /* data loaded (LW) */

} InstructionContext;

/* ---- Extern declarations for global state ------------------ */
extern int32_t memory[MEM_SIZE];
extern int32_t registers[NUM_REGISTERS];
extern int32_t PC;
extern int clock_cycle;
extern int instruction_count;    /* total instructions loaded */
extern int program_done;         /* 1 when all instructions retired */

/* Pipeline stage registers */
extern InstructionContext IF_stage;
extern InstructionContext ID_stage;
extern InstructionContext EX_stage;
extern InstructionContext MEM_stage;
extern InstructionContext WB_stage;

/* ---- Utility ----------------------------------------------- */
const char *opcode_to_string(int opcode);
InstructionContext empty_context(void);
int get_format(int opcode);   /* 0 = R, 1 = I, 2 = J */

/* Format types */
#define FORMAT_R  0
#define FORMAT_I  1
#define FORMAT_J  2

#endif /* GLOBALS_H */
