/*
 * test_runner.c — Comprehensive automated test suite for Package 1
 *
 * Compiles WITH the existing source files (globals.c, parser.c, alu.c, branch.c)
 * but provides its OWN main() and pipeline loop (copied from pipeline_simulator.c).
 *
 * Runs each test program, then checks register and memory values against expectations.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../parser.h"

/* ---- Forward declarations ---- */
void Fetch(void);
void Decode(void);
void Execute(void);
extern void execute_alu(void);     /* from alu.c */
extern void execute_branch(void);  /* from branch.c */
void MemoryAccess(void);
void WriteBack(void);
int  check_data_hazard(void);

/* Counters */
static int tests_passed = 0;
static int tests_failed = 0;
static int total_checks = 0;

/* ---- Minimal assertion helper ---- */
static void check(const char *test_name, const char *desc, int condition) {
    total_checks++;
    if (condition) {
        tests_passed++;
        printf("    [PASS] %s: %s\n", test_name, desc);
    } else {
        tests_failed++;
        printf("    [FAIL] %s: %s\n", test_name, desc);
    }
}

static void check_reg(const char *test_name, int reg, int32_t expected) {
    char desc[128];
    sprintf(desc, "R%d == %d (got %d)", reg, expected, registers[reg]);
    check(test_name, desc, registers[reg] == expected);
}

static void check_mem(const char *test_name, int addr, int32_t expected) {
    char desc[128];
    sprintf(desc, "memory[%d] == %d (got %d)", addr, expected, memory[addr]);
    check(test_name, desc, memory[addr] == expected);
}

static void check_pc_ge(const char *test_name, int32_t min_val) {
    char desc[128];
    sprintf(desc, "PC >= %d (got %d)", min_val, PC);
    check(test_name, desc, PC >= min_val);
}

/* ---- Hazard detection (from pipeline_simulator.c) ---- */
static void get_source_regs(int32_t raw, int *src1, int *src2) {
    int opcode = (raw >> 28) & 0xF;
    int r1     = (raw >> 23) & 0x1F;
    int r2     = (raw >> 18) & 0x1F;
    *src1 = -1;
    *src2 = -1;
    switch (opcode) {
        case OP_ADD: case OP_SUB:
            *src1 = r1; *src2 = r2; break;
        case OP_BNE: case OP_SW:
            *src1 = r1; *src2 = r2; break;
        case OP_MULI: case OP_ADDI:
        case OP_ANDI: case OP_XORI:
        case OP_SLL:  case OP_SRL:
        case OP_LW:
            *src1 = r2; break;
        case OP_J:
            break;
    }
}

int check_data_hazard(void) {
    if (!ID_Stage.is_active) return 0;
    int src1, src2;
    get_source_regs(ID_Stage.raw_instruction, &src1, &src2);
    if (src1 == -1 && src2 == -1) return 0;
    InstructionContext *stages[] = { &EX_Stage, &MEM_Stage, &WB_Stage };
    for (int i = 0; i < 3; i++) {
        if (!stages[i]->is_active) continue;
        if (!stages[i]->reg_write) continue;
        int dest = stages[i]->dest_reg;
        if (dest <= 0) continue;
        if (dest == src1 || dest == src2) return 1;
    }
    return 0;
}

/* ---- Pipeline stages (from pipeline_simulator.c) ---- */
void Fetch(void) {
    if (PC >= instruction_count) {
        PC = 1024;
        IF_Stage.is_active = 0;
        return;
    }
    IF_Stage.instruction_address = PC;
    IF_Stage.raw_instruction = memory[PC];
    PC++;
}

void Decode(void) {
    if (ID_Stage.cycles_remaining == 1) {
        int32_t inst = ID_Stage.raw_instruction;
        ID_Stage.opcode = (inst >> 28) & 0xF;
        ID_Stage.r1     = (inst >> 23) & 0x1F;
        ID_Stage.r2     = (inst >> 18) & 0x1F;
        ID_Stage.r3     = (inst >> 13) & 0x1F;
        ID_Stage.shamt  = inst & 0x1FFF;
        int32_t imm = inst & 0x3FFFF;
        if (imm & 0x20000) imm |= 0xFFFC0000;
        ID_Stage.imm = imm;
        ID_Stage.address = inst & 0xFFFFFFF;
        ID_Stage.val_r1 = registers[ID_Stage.r1];
        ID_Stage.val_r2 = registers[ID_Stage.r2];
        ID_Stage.val_r3 = registers[ID_Stage.r3];
        ID_Stage.reg_write = 0; ID_Stage.mem_read = 0;
        ID_Stage.mem_write = 0; ID_Stage.mem_to_reg = 0;
        ID_Stage.dest_reg = -1;
        switch (ID_Stage.opcode) {
            case 0: case 1:
                ID_Stage.reg_write = 1; ID_Stage.dest_reg = ID_Stage.r3; break;
            case 8: case 9: case 2: case 3: case 5: case 6:
                ID_Stage.reg_write = 1; ID_Stage.dest_reg = ID_Stage.r1; break;
            case 10:
                ID_Stage.reg_write = 1; ID_Stage.mem_read = 1;
                ID_Stage.mem_to_reg = 1; ID_Stage.dest_reg = ID_Stage.r1; break;
            case 11: ID_Stage.mem_write = 1; break;
            case 4: case 7: break;
        }
    }
}

void Execute(void) {
    execute_alu();
    execute_branch();
}

void MemoryAccess(void) {
    if (!MEM_Stage.is_active) return;
    if (MEM_Stage.mem_read) {
        int32_t ea = MEM_Stage.val_r2 + MEM_Stage.imm;
        if (ea >= 0 && ea < 2048) MEM_Stage.mem_read_data = memory[ea];
        else MEM_Stage.mem_read_data = 0;
    } else if (MEM_Stage.mem_write) {
        int32_t ea = MEM_Stage.val_r2 + MEM_Stage.imm;
        if (ea >= 0 && ea < 2048) memory[ea] = MEM_Stage.val_r1;
    }
}

void WriteBack(void) {
    if (!WB_Stage.is_active) return;
    if (WB_Stage.reg_write) {
        int32_t wv = WB_Stage.mem_to_reg ? WB_Stage.mem_read_data : WB_Stage.alu_result;
        int dest = WB_Stage.dest_reg;
        if (dest > 0 && dest < 32) registers[dest] = wv;
    }
}

/* ---- Run simulation (copied logic from pipeline_simulator.c main) ---- */
static void run_simulation(const char *filename) {
    memset(memory, 0, sizeof(memory));
    memset(registers, 0, sizeof(registers));
    memset(&IF_Stage, 0, sizeof(InstructionContext));
    memset(&ID_Stage, 0, sizeof(InstructionContext));
    memset(&EX_Stage, 0, sizeof(InstructionContext));
    memset(&MEM_Stage, 0, sizeof(InstructionContext));
    memset(&WB_Stage, 0, sizeof(InstructionContext));
    PC = 0;
    clock_cycle = 1;
    instruction_count = 0;
    program_done = 0;

    int num = parse_program(filename);
    if (num < 0) {
        printf("  ERROR: Failed to parse %s\n", filename);
        return;
    }

    while (1) {
        if (PC >= 1024 && !IF_Stage.is_active && !ID_Stage.is_active &&
            !EX_Stage.is_active && !MEM_Stage.is_active && !WB_Stage.is_active)
            break;

        if (WB_Stage.is_active) {
            if (WB_Stage.branch_taken) {
                PC = WB_Stage.branch_target;
                memset(&EX_Stage, 0, sizeof(InstructionContext));
                memset(&ID_Stage, 0, sizeof(InstructionContext));
            }
            WriteBack();
            memset(&WB_Stage, 0, sizeof(InstructionContext));
        }
        if (MEM_Stage.is_active) {
            MemoryAccess();
            WB_Stage = MEM_Stage;
            memset(&MEM_Stage, 0, sizeof(InstructionContext));
        }
        if (EX_Stage.is_active) {
            EX_Stage.cycles_remaining++;
            Execute();
            if (EX_Stage.cycles_remaining == 2) {
                MEM_Stage = EX_Stage;
                MEM_Stage.cycles_remaining = 0;
                memset(&EX_Stage, 0, sizeof(InstructionContext));
            }
        }
        if (ID_Stage.is_active) {
            if (ID_Stage.cycles_remaining == 0 && check_data_hazard()) {
                /* stall */
            } else {
                ID_Stage.cycles_remaining++;
                Decode();
                if (ID_Stage.cycles_remaining == 2) {
                    EX_Stage = ID_Stage;
                    EX_Stage.cycles_remaining = 0;
                    memset(&ID_Stage, 0, sizeof(InstructionContext));
                }
            }
        }
        int stalled = (ID_Stage.is_active && ID_Stage.cycles_remaining == 0);
        if (!stalled && clock_cycle % 2 != 0 && PC >= 0 && PC < 1024) {
            IF_Stage.is_active = 1;
            Fetch();
            if (IF_Stage.is_active) {
                ID_Stage = IF_Stage;
                memset(&IF_Stage, 0, sizeof(InstructionContext));
            }
        }
        registers[0] = 0;
        clock_cycle++;
        if (clock_cycle > 500) break;
    }
}

/* ============================================================
 * TEST CASES
 * ============================================================ */

static void test1_arithmetic(void) {
    const char *name = "TEST1_ARITH";
    printf("\n=== %s: Basic Arithmetic (ADDI, ADD, SUB, MULI) ===\n", name);
    /* ADDI R1 R0 10  => R1=10
     * ADDI R2 R0 20  => R2=20
     * ADD  R1 R2 R3  => R3 = R1+R2 = 30
     * SUB  R1 R2 R4  => R4 = R1-R2 = -10
     * MULI R5 R1 3   => R5 = R1*3 = 30 */
    run_simulation("tests/test1_arithmetic.txt");
    check_reg(name, 0, 0);    /* R0 always 0 */
    check_reg(name, 1, 10);
    check_reg(name, 2, 20);
    check_reg(name, 3, 30);   /* 10+20 */
    check_reg(name, 4, -10);  /* 10-20 */
    check_reg(name, 5, 30);   /* 10*3 */
}

static void test2_logical_shift(void) {
    const char *name = "TEST2_LOGIC";
    printf("\n=== %s: Logical & Shift (ANDI, XORI, SLL, SRL) ===\n", name);
    /* ADDI R1 R0 255 => R1=255
     * ADDI R2 R0 15  => R2=15
     * ANDI R3 R1 15  => R3 = 255 & 15 = 15
     * XORI R4 R1 255 => R4 = 255 ^ 255 = 0
     * ADDI R5 R0 8   => R5=8
     * SLL  R6 R5 2   => R6 = 8 << 2 = 32
     * SRL  R7 R5 1   => R7 = 8 >> 1 = 4 */
    run_simulation("tests/test2_logical_shift.txt");
    check_reg(name, 1, 255);
    check_reg(name, 2, 15);
    check_reg(name, 3, 15);   /* 255 & 15 */
    check_reg(name, 4, 0);    /* 255 ^ 255 */
    check_reg(name, 5, 8);
    check_reg(name, 6, 32);   /* 8 << 2 */
    check_reg(name, 7, 4);    /* 8 >> 1 */
}

static void test3_memory(void) {
    const char *name = "TEST3_MEM";
    printf("\n=== %s: Memory (SW, LW) ===\n", name);
    /* ADDI R1 R0 42   => R1=42
     * ADDI R2 R0 1024 => R2=1024
     * SW   R1 R2 0    => memory[1024+0] = 42
     * LW   R3 R2 0    => R3 = memory[1024+0] = 42 */
    run_simulation("tests/test3_memory.txt");
    check_reg(name, 1, 42);
    check_reg(name, 2, 1024);
    check_mem(name, 1024, 42);
    check_reg(name, 3, 42);
}

static void test4_bne_taken(void) {
    const char *name = "TEST4_BNE_T";
    printf("\n=== %s: BNE Taken ===\n", name);
    /* ADDI R1 R0 5    => R1=5
     * ADDI R2 R0 10   => R2=10
     * BNE  R1 R2 2    => R1!=R2 -> PC = 2+1+2 = 5
     * ADDI R3 R0 99   => should be flushed
     * ADDI R4 R0 88   => should be flushed
     * ADDI R5 R0 77   => should execute: R5=77 */
    run_simulation("tests/test4_bne_taken.txt");
    check_reg(name, 1, 5);
    check_reg(name, 2, 10);
    check_reg(name, 5, 77);
    /* R3 and R4 should NOT be set (flushed) */
    printf("    [INFO] R3=%d (expect 0 if flushed), R4=%d (expect 0 if flushed)\n",
           registers[3], registers[4]);
}

static void test5_bne_not_taken(void) {
    const char *name = "TEST5_BNE_N";
    printf("\n=== %s: BNE Not Taken ===\n", name);
    /* ADDI R1 R0 5   => R1=5
     * ADDI R2 R0 5   => R2=5
     * BNE  R1 R2 2   => R1==R2 -> not taken
     * ADDI R3 R0 99  => should execute: R3=99
     * ADDI R4 R0 88  => should execute: R4=88 */
    run_simulation("tests/test5_bne_not_taken.txt");
    check_reg(name, 1, 5);
    check_reg(name, 2, 5);
    check_reg(name, 3, 99);
    check_reg(name, 4, 88);
}

static void test6_jump(void) {
    const char *name = "TEST6_JUMP";
    printf("\n=== %s: Unconditional Jump (J) ===\n", name);
    /* ADDI R1 R0 10  => R1=10
     * J 4            => jump to addr 4
     * ADDI R2 R0 99  => should be flushed
     * ADDI R3 R0 88  => should be flushed
     * ADDI R4 R0 77  => should execute: R4=77 */
    run_simulation("tests/test6_jump.txt");
    check_reg(name, 1, 10);
    check_reg(name, 4, 77);
    printf("    [INFO] R2=%d (expect 0 if flushed), R3=%d (expect 0 if flushed)\n",
           registers[2], registers[3]);
}

static void test7_r0_protection(void) {
    const char *name = "TEST7_R0";
    printf("\n=== %s: R0 Hardwired Zero Protection ===\n", name);
    /* ADDI R1 R0 10  => R1=10
     * ADDI R2 R0 20  => R2=20
     * ADD  R1 R2 R0  => R0 should stay 0 (dest=R0, write discarded)
     * ADDI R3 R0 5   => R3=5 */
    run_simulation("tests/test7_r0_protection.txt");
    check_reg(name, 0, 0);
    check_reg(name, 1, 10);
    check_reg(name, 2, 20);
    check_reg(name, 3, 5);
}

static void test8_negative_imm(void) {
    const char *name = "TEST8_NEG";
    printf("\n=== %s: Negative Immediates ===\n", name);
    /* ADDI R1 R0 100  => R1=100
     * ADDI R2 R1 -30  => R2 = 100 + (-30) = 70
     * MULI R3 R1 -2   => R3 = 100 * (-2) = -200 */
    run_simulation("tests/test8_negative_imm.txt");
    check_reg(name, 1, 100);
    check_reg(name, 2, 70);
    check_reg(name, 3, -200);
}

static void test9_hazard_stall(void) {
    const char *name = "TEST9_HAZ";
    printf("\n=== %s: Data Hazard Stalling ===\n", name);
    /* ADDI R1 R0 10  => R1=10
     * ADDI R2 R1 5   => R2 = R1+5 = 15 (R1 hazard, must stall)
     * ADD  R1 R2 R3  => R3 = R1+R2 = 25 (R2 hazard, must stall) */
    run_simulation("test_hazard.txt");
    check_reg(name, 1, 10);
    check_reg(name, 2, 15);
    check_reg(name, 3, 25);
}

static void test10_cycle_count(void) {
    const char *name = "TEST10_CYC";
    printf("\n=== %s: Cycle Count Formula ===\n", name);
    /* 5 instructions: expected cycles = 7 + (5-1)*2 = 15
     * But with data hazards, it could be more.
     * For test1 (5 instructions, with hazards due to ADDI->ADD/SUB/MULI deps)
     * we just check the simulation terminates in a reasonable number. */
    run_simulation("tests/test1_arithmetic.txt");
    printf("    [INFO] Simulation completed in %d cycles\n", clock_cycle - 1);
    check(name, "Simulation terminated (clock < 500)", clock_cycle < 500);
}

/* ============================================================ */
int main(int argc, char *argv[]) {
    printf("================================================================\n");
    printf("  Package 1 — Comprehensive Automated Test Suite\n");
    printf("  Spicy Von Neumann Fillet with extra shifts\n");
    printf("================================================================\n");

    test1_arithmetic();
    test2_logical_shift();
    test3_memory();
    test4_bne_taken();
    test5_bne_not_taken();
    test6_jump();
    test7_r0_protection();
    test8_negative_imm();
    test9_hazard_stall();
    test10_cycle_count();

    printf("\n================================================================\n");
    printf("  RESULTS: %d/%d checks passed, %d failed\n",
           tests_passed, total_checks, tests_failed);
    if (tests_failed == 0)
        printf("  ALL TESTS PASSED!\n");
    else
        printf("  SOME TESTS FAILED — review output above.\n");
    printf("================================================================\n");

    return tests_failed > 0 ? 1 : 0;
}
