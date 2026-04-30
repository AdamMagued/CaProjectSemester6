/*
 * alu.c - Execution Unit: ALU Engineer (EX - Part 1)
 * 
 * Package 1: Spicy Von Neumann Fillet with extra shifts
 * CSEN 601 - Spring 26
 *
 * This file implements the ALU logic for the Execute stage of the pipeline.
 * It handles all arithmetic, logical, and shift instructions:
 *   ADD, SUB, MULI, ADDI, ANDI, XORI, SLL, SRL
 *
 * The Execute stage takes 2 clock cycles to complete.
 * On the first cycle, the ALU begins computation (cycles_remaining is set to 2).
 * On the second cycle, the ALU finishes and the result is stored in alu_result.
 *
 * Branch/Jump instructions (BNE, J) and Memory instructions (LW, SW) are NOT
 * handled here — they belong to EX-Part 2 and MEM respectively.
 */

#include <stdint.h>
#include <stdio.h>
#include "pipeline.h"  /* Shared header with InstructionContext, opcodes, globals */

/*
 * execute_alu()
 *
 * Called every clock cycle that the EX stage is active.
 * Uses EX_Stage (the global pipeline register for the Execute stage).
 *
 * Behavior:
 *   - If EX_Stage.is_active == 0, do nothing (stage is empty).
 *   - If cycles_remaining == 2 (first cycle in EX):
 *       Perform the actual ALU computation and store in alu_result.
 *       Decrement cycles_remaining to 1.
 *       Print that computation has begun.
 *   - If cycles_remaining == 1 (second cycle in EX):
 *       The result is already computed; just decrement cycles_remaining to 0.
 *       Print that execution is complete.
 *       (The pipeline controller will advance this to MEM on the next cycle.)
 */
void execute_alu(void) {
    /* Skip if the Execute stage has no active instruction */
    if (!EX_Stage.is_active) {
        return;
    }

    int opcode = EX_Stage.opcode;

    /*
     * First cycle of Execute (cycles_remaining == 2):
     * We perform the actual computation here, so the result is ready
     * by the time the second cycle ends.
     */
    if (EX_Stage.cycles_remaining == 2) {

        /* Read operand values that were captured during Decode */
        int32_t val_r2 = EX_Stage.val_r2;  /* Value of source register R2 */
        int32_t val_r3 = EX_Stage.val_r3;  /* Value of source register R3 (R-Type only) */
        int32_t imm    = EX_Stage.imm;     /* Sign-extended immediate (I-Type only) */
        int shamt      = EX_Stage.shamt;   /* Shift amount (SLL/SRL only) */

        int32_t result = 0;

        switch (opcode) {

            /* --------------------------------------------------------
             * R-Type Arithmetic
             * -------------------------------------------------------- */

            case OP_ADD:
                /*
                 * ADD R1, R2, R3
                 * R1 = R2 + R3
                 */
                result = val_r2 + val_r3;
                printf("  [EX] ADD: %d + %d = %d\n", val_r2, val_r3, result);
                break;

            case OP_SUB:
                /*
                 * SUB R1, R2, R3
                 * R1 = R2 - R3
                 */
                result = val_r2 - val_r3;
                printf("  [EX] SUB: %d - %d = %d\n", val_r2, val_r3, result);
                break;

            /* --------------------------------------------------------
             * I-Type Arithmetic
             * -------------------------------------------------------- */

            case OP_MULI:
                /*
                 * MULI R1, R2, IMM
                 * R1 = R2 * IMM
                 * IMM is a signed 18-bit immediate (sign-extended to 32 bits).
                 */
                result = val_r2 * imm;
                printf("  [EX] MULI: %d * %d = %d\n", val_r2, imm, result);
                break;

            case OP_ADDI:
                /*
                 * ADDI R1, R2, IMM
                 * R1 = R2 + IMM
                 * IMM is a signed 18-bit immediate (sign-extended to 32 bits).
                 */
                result = val_r2 + imm;
                printf("  [EX] ADDI: %d + %d = %d\n", val_r2, imm, result);
                break;

            /* --------------------------------------------------------
             * I-Type Logical
             * -------------------------------------------------------- */

            case OP_ANDI:
                /*
                 * ANDI R1, R2, IMM
                 * R1 = R2 & IMM
                 * Bitwise AND between R2's value and the sign-extended immediate.
                 */
                result = val_r2 & imm;
                printf("  [EX] ANDI: %d & %d = %d\n", val_r2, imm, result);
                break;

            case OP_XORI:
                /*
                 * XORI R1, R2, IMM
                 * R1 = R2 ^ IMM
                 * Bitwise XOR between R2's value and the sign-extended immediate.
                 */
                result = val_r2 ^ imm;
                printf("  [EX] XORI: %d ^ %d = %d\n", val_r2, imm, result);
                break;

            /* --------------------------------------------------------
             * R-Type Shift Operations
             * -------------------------------------------------------- */

            case OP_SLL:
                /*
                 * SLL R1, R2, SHAMT
                 * R1 = R2 << SHAMT
                 * Logical shift left. SHAMT is always positive (5-bit unsigned).
                 * R3 is 0 in the instruction format for shift instructions.
                 */
                result = val_r2 << shamt;
                printf("  [EX] SLL: %d << %d = %d\n", val_r2, shamt, result);
                break;

            case OP_SRL:
                /*
                 * SRL R1, R2, SHAMT
                 * R1 = R2 >> SHAMT  (logical, zero-fill)
                 * We cast to uint32_t to ensure a logical (not arithmetic) shift.
                 * SHAMT is always positive (5-bit unsigned).
                 * R3 is 0 in the instruction format for shift instructions.
                 */
                result = (int32_t)((uint32_t)val_r2 >> shamt);
                printf("  [EX] SRL: %d >>> %d = %d\n", val_r2, shamt, result);
                break;

            default:
                /*
                 * Non-ALU opcodes (BNE, J, LW, SW) are handled elsewhere.
                 * We intentionally do NOT set alu_result for those.
                 */
                break;
        }

        /* Store the computed result in the pipeline register */
        EX_Stage.alu_result = result;

        /* Move from first cycle to second cycle */
        EX_Stage.cycles_remaining = 1;

        printf("  [EX] Cycle 1 of 2 complete for instruction at address %d (opcode %d)\n",
               EX_Stage.instruction_address, opcode);

    } else if (EX_Stage.cycles_remaining == 1) {
        /*
         * Second cycle of Execute:
         * The ALU result was already computed on the first cycle.
         * We just mark it as done so the pipeline controller can
         * advance this instruction to the MEM stage.
         */
        EX_Stage.cycles_remaining = 0;

        printf("  [EX] Cycle 2 of 2 complete for instruction at address %d (opcode %d). "
               "ALU Result = %d\n",
               EX_Stage.instruction_address, opcode, EX_Stage.alu_result);
    }
}
