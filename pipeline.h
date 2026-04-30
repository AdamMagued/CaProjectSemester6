/*
 * pipeline.h - Shared definitions for the Package 1 pipelined processor
 *
 * Contains:
 *   - Instruction opcodes
 *   - Pipeline "baton" struct (InstructionContext)
 *   - External declarations for global hardware state
 *
 * Every .c file in the project should #include "pipeline.h"
 */

#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdint.h>

/* ================================================================
 * Instruction Opcodes (Package 1)
 * Opcodes 0-11, in order from the project description.
 * ================================================================ */
#define OP_ADD  0
#define OP_SUB  1
#define OP_MULI 2
#define OP_ADDI 3
#define OP_BNE  4
#define OP_ANDI 5
#define OP_XORI 6
#define OP_J    7
#define OP_SLL  8
#define OP_SRL  9
#define OP_LW   10
#define OP_SW   11

/* ================================================================
 * Pipeline "Baton" Struct
 *
 * One instance per pipeline stage. As an instruction moves through
 * the pipeline, its data is copied from one stage's struct to the
 * next, carrying all decoded fields and intermediate results.
 * ================================================================ */
typedef struct {
    int is_active;           /* 1 if an instruction is in this stage, 0 if empty/flushed */
    int cycles_remaining;    /* Tracks multi-cycle stages (ID=2, EX=2) */

    /* --- Populated during FETCH --- */
    int32_t instruction_address; /* Memory address this instruction was fetched from */
    int32_t raw_instruction;     /* The raw 32-bit binary pulled from memory */

    /* --- Populated during DECODE --- */
    /* All fields are parsed regardless of instruction type */
    int opcode;
    int r1;        /* Destination register (usually) */
    int r2;        /* Source register 1 */
    int r3;        /* Source register 2 (R-Type) */
    int shamt;     /* Shift amount (SLL/SRL) */
    int32_t imm;   /* Immediate value (I-Type/Branches), signed (18-bit sign-extended) */
    int32_t address; /* Jump address (J-Type, 28 bits) */

    /* Values read from register file during Decode */
    int32_t val_r1; /* Value of r1 (needed for ADD/SUB where R1 is a source, and BNE) */
    int32_t val_r2;
    int32_t val_r3;

    /* --- Populated during EXECUTE --- */
    int32_t alu_result;      /* Result of math/logic operations */
    int branch_taken;        /* 1 if a branch/jump condition was met */
    int32_t branch_target;   /* Calculated address to jump to (if branch_taken) */

    /* --- Populated during MEMORY --- */
    int32_t mem_read_data;   /* Data loaded from memory (for LW) */

} InstructionContext;

/* ================================================================
 * Global Hardware State (defined in main.c, declared here)
 * ================================================================ */

/* System clock */
extern int clock_cycle;

/* Main Memory: 2048 rows × 32 bits
 *   [0..1023]    = Instruction memory
 *   [1024..2047] = Data memory
 */
extern int32_t memory[2048];

/* Register File: 32 General-Purpose Registers
 *   registers[0] is hardwired to 0 (must NEVER be overwritten)
 */
extern int32_t registers[32];

/* Program Counter */
extern int32_t PC;

/* Pipeline Stage Registers */
extern InstructionContext IF_Stage;
extern InstructionContext ID_Stage;
extern InstructionContext EX_Stage;
extern InstructionContext MEM_Stage;
extern InstructionContext WB_Stage;

/* ================================================================
 * Function Prototypes
 * Each pipeline stage has its own function, implemented in its own .c file.
 * ================================================================ */

/* Front-End: Fetch & Decode (fetch_decode.c) */
void fetch(void);
void decode(void);

/* Execution Unit: ALU (alu.c) */
void execute_alu(void);

/* Execution Unit: Branch & Hazard Manager (branch.c) */
void execute_branch(void);

/* Back-End: Memory & Write-Back (mem_wb.c) */
void memory_access(void);
void write_back(void);

/* Parser & Memory Initializer (parser.c) */
int load_program(const char *filename);

#endif /* PIPELINE_H */
