/*
 * branch.c - Execution Unit: Branch & Hazard Manager (EX - Part 2)
 *
 * Package 1: Spicy Von Neumann Fillet with extra shifts
 * CSEN 601 - Spring 26
 *
 * This file implements the branch/jump logic for the Execute stage.
 * It handles the control-flow instructions:
 *   BNE  (Branch if Not Equal)
 *   J    (Unconditional Jump)
 *
 * The Execute stage takes 2 clock cycles to complete.
 * On the first cycle (cycles_remaining == 1), we evaluate the branch
 * condition and compute the target address.
 * On the second cycle (cycles_remaining == 2), the result is ready and
 * the main loop will advance this instruction to MEM.
 *
 * The flush of wrong-path instructions (ID and EX stages) is handled
 * by the main pipeline loop when the branch_taken flag reaches WB.
 */

#include <stdint.h>
#include <stdio.h>

/* ================================================================
 * External references to pipeline_simulator.c globals
 * ================================================================
 *
 * We declare only what we need here.  The canonical struct definition
 * and global variables live in pipeline_simulator.c.  By re-declaring
 * the struct identically and using 'extern', the linker connects us
 * to the same EX_Stage instance used by the rest of the pipeline.
 * ================================================================ */

/* --- Instruction Opcodes (must match pipeline_simulator.c) --- */
#define OP_BNE  4
#define OP_J    7

/* --- Pipeline Baton Struct (must match pipeline_simulator.c) --- */
typedef struct {
    int is_active;
    int cycles_remaining;

    int32_t instruction_address;
    int32_t raw_instruction;

    int opcode;
    int r1;
    int r2;
    int r3;
    int shamt;
    int32_t imm;
    int32_t address;

    int32_t val_r1;
    int32_t val_r2;
    int32_t val_r3;

    int dest_reg;

    int32_t alu_result;
    int branch_taken;
    int32_t branch_target;

    int32_t mem_read_data;

    int reg_write;
    int mem_read;
    int mem_write;
    int mem_to_reg;

} InstructionContext;

/* The actual EX_Stage lives in pipeline_simulator.c */
extern InstructionContext EX_Stage;

/*
 * execute_branch()
 *
 * Called every clock cycle that the EX stage is active.
 * Uses EX_Stage (the global pipeline register for the Execute stage).
 *
 * Only acts on BNE and J instructions; returns immediately for all others.
 *
 * Behavior:
 *   - If EX_Stage.is_active == 0, do nothing.
 *   - If the opcode is not BNE or J, do nothing (those belong to execute_alu).
 *   - If cycles_remaining == 1 (first cycle in EX):
 *       Evaluate branch condition / compute jump target.
 *       Set branch_taken and branch_target on the baton.
 *   - If cycles_remaining == 2 (second cycle in EX):
 *       Print completion message; the main loop advances to MEM.
 */
void execute_branch(void) {
    /* Skip if the Execute stage has no active instruction */
    if (!EX_Stage.is_active) {
        return;
    }

    int opcode = EX_Stage.opcode;

    /* Only handle branch/jump opcodes */
    if (opcode != OP_BNE && opcode != OP_J) {
        return;
    }

    /*
     * First cycle of Execute (cycles_remaining == 1):
     * Evaluate the branch condition and compute the target address.
     */
    if (EX_Stage.cycles_remaining == 1) {

        switch (opcode) {

            case OP_BNE:
                /*
                 * BNE R1, R2, IMM
                 * If R1 != R2, branch to (PC_of_this_instruction + 1 + IMM).
                 *
                 * - val_r1 holds the value of R1 (read during Decode).
                 * - val_r2 holds the value of R2 (read during Decode).
                 * - imm is the signed 18-bit offset (sign-extended).
                 * - instruction_address is the PC where this BNE was fetched.
                 *
                 * We set branch_taken and branch_target here.
                 * The main pipeline loop will use these fields to flush
                 * wrong-path instructions and redirect the PC when the
                 * instruction reaches WB.
                 */
                if (EX_Stage.val_r1 != EX_Stage.val_r2) {
                    EX_Stage.branch_taken  = 1;
                    EX_Stage.branch_target = EX_Stage.instruction_address + 1 + EX_Stage.imm;
                    printf("  [EX] BNE: R1(%d) != R2(%d) -> TAKEN, target = %d\n",
                           EX_Stage.val_r1, EX_Stage.val_r2, EX_Stage.branch_target);
                } else {
                    EX_Stage.branch_taken  = 0;
                    printf("  [EX] BNE: R1(%d) == R2(%d) -> NOT TAKEN\n",
                           EX_Stage.val_r1, EX_Stage.val_r2);
                }
                break;

            case OP_J:
                /*
                 * J ADDRESS
                 * Unconditional jump to target = PC[31:28] || ADDRESS.
                 *
                 * The 28-bit address field was parsed during Decode and stored
                 * in EX_Stage.address.  The upper 4 bits come from the PC
                 * that was current when the instruction was fetched
                 * (instruction_address).  In practice, since all instruction
                 * addresses fit within 0-1023, the upper 4 bits are always 0,
                 * but we compute it correctly for completeness.
                 *
                 * branch_taken is always 1 (unconditional).
                 * The main loop will flush wrong-path instructions and
                 * redirect the PC when this instruction reaches WB.
                 */
                EX_Stage.branch_taken  = 1;
                EX_Stage.branch_target =
                    (EX_Stage.instruction_address & 0xF0000000) | EX_Stage.address;
                printf("  [EX] J: Unconditional jump -> target = %d\n",
                       EX_Stage.branch_target);
                break;
        }

        printf("  [EX] Cycle 1 of 2 complete for branch/jump at address %d (opcode %d)\n",
               EX_Stage.instruction_address, opcode);

    } else if (EX_Stage.cycles_remaining == 2) {
        /*
         * Second cycle of Execute:
         * The branch decision was already made on the first cycle.
         * The main loop will pass this instruction to MEM after this call.
         */
        printf("  [EX] Cycle 2 of 2 complete for branch/jump at address %d (opcode %d). "
               "branch_taken = %d, target = %d\n",
               EX_Stage.instruction_address, opcode,
               EX_Stage.branch_taken, EX_Stage.branch_target);
    }
}
