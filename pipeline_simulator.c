#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "parser.h"

/*
 * All opcodes, InstructionContext struct, and global variables
 * (memory, registers, PC, clock_cycle, pipeline stages) are
 * defined in globals.h / globals.c and included via parser.h.
 */

// ==========================================
// 3. STAGE FUNCTION PROTOTYPES (For the rest of the team)
// ==========================================
void Fetch();
void Decode();
void Execute();
void execute_alu(void);    // Implemented in alu.c
void execute_branch(void); // Implemented in branch.c
void MemoryAccess();
void WriteBack();
void PrintCycleState();
void PrintFinalState();
int  check_load_use_hazard();
int32_t forward_value(int reg_idx, int32_t default_val);

// ==========================================
// 4. THE MAIN CLOCK ENGINE
// ==========================================
int main(int argc, char *argv[]) {
    const char *filename = "program.txt";
    if (argc > 1) filename = argv[1];

    // Clear hardware state
    memset(memory, 0, sizeof(memory));
    memset(registers, 0, sizeof(registers));
    PC = 0;

    // Role 2: Parse assembly file and load into instruction memory
    int num_instructions = parse_program(filename);
    if (num_instructions < 0) {
        fprintf(stderr, "Failed to parse program.\n");
        return 1;
    }
    printf("\n========================================\n");
    printf("  Pipeline Simulation Starting\n");
    printf("  %d instructions loaded\n", num_instructions);
    printf("========================================\n\n");
    
    // The processor runs until all pipeline stages are empty AND we are out of instructions
    while (1) {
        // Stop condition: If PC is out of instruction memory bounds and pipeline is empty
        if (PC >= 1024 && !IF_Stage.is_active && !ID_Stage.is_active && 
            !EX_Stage.is_active && !MEM_Stage.is_active && !WB_Stage.is_active) {
            break; 
        }

        // ----------------------------------------------------
        // PROCESS STAGES (Reverse order to prevent overwriting)
        // ----------------------------------------------------
        
        // 1. WRITE BACK (Takes 1 cycle, runs on Odd cycles usually)
        if (WB_Stage.is_active) {
            
            // HAZARD HANDLING: Delayed flush matching the TA's X+3 timeline.
            // The branch_taken flag rides the baton through EX -> MEM -> WB.
            // By the time it reaches WB, the "wrong" instructions have naturally
            // populated EX and ID, exactly as the document's trace shows.
            if (WB_Stage.branch_taken) {
                PC = WB_Stage.branch_target; // Update PC for the new fetch below
                memset(&EX_Stage, 0, sizeof(InstructionContext)); // Drop instruction in EX
                memset(&ID_Stage, 0, sizeof(InstructionContext)); // Drop instruction in ID
            }
            
            WriteBack();
            // Instruction is finished, clear it out.
            memset(&WB_Stage, 0, sizeof(InstructionContext)); 
        }

        // 2. MEMORY (Takes 1 cycle, runs on Even cycles)
        if (MEM_Stage.is_active) {
            MemoryAccess();
            WB_Stage = MEM_Stage; // Direct transfer to WB
            memset(&MEM_Stage, 0, sizeof(InstructionContext));
        }

        // 3. EXECUTE (Takes 2 cycles)
        if (EX_Stage.is_active) {
            EX_Stage.cycles_remaining++;
            
            // Execute() is called twice, ensure no duplicate state changes inside it!
            Execute();
            
            // Only pass to MEM if it has finished its 2nd cycle
            if (EX_Stage.cycles_remaining == 2) {
                MEM_Stage = EX_Stage; // Direct transfer to MEM (branch data rides along!)
                MEM_Stage.cycles_remaining = 0; // Reset for MEM stage
                memset(&EX_Stage, 0, sizeof(InstructionContext));
            }
        }

        // 4. DECODE (Takes 2 cycles) — with forwarding + load-use stall
        if (ID_Stage.is_active) {
            if (ID_Stage.cycles_remaining == 0 && check_load_use_hazard()) {
                printf("  [STALL] Load-use hazard — pipeline stalled\n");
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

        // 5. FETCH (Runs only on ODD clock cycles, NOT during a stall, NOT if ID is occupied)
        int stalled = (ID_Stage.is_active && ID_Stage.cycles_remaining == 0);
        if (!stalled && !ID_Stage.is_active && clock_cycle % 2 != 0 && PC >= 0 && PC < 1024) {
            IF_Stage.is_active = 1;
            Fetch();
            if (IF_Stage.is_active) {
                ID_Stage = IF_Stage; // Direct transfer to ID
                memset(&IF_Stage, 0, sizeof(InstructionContext));
            }
        }

        // Enforce Hardwired Zero Register
        registers[0] = 0;

        // Print outputs for grading
        PrintCycleState();

        clock_cycle++;
        
        // Safety break to prevent infinite loop
        if(clock_cycle > 500) break; 
    }

    printf("\n========================================\n");
    printf("  Simulation Complete in %d Cycles\n", clock_cycle - 1);
    printf("========================================\n");

    // Print final state of all registers and memory (required by spec)
    PrintFinalState();
    return 0;
}

// ==========================================
// DATA FORWARDING + LOAD-USE HAZARD
// ==========================================

/*
 * Forward a register value from EX/MEM/WB if a producer hasn't written back yet.
 * Priority: EX > MEM > WB (most recent program-order instruction first).
 */
int32_t forward_value(int reg_idx, int32_t default_val) {
    if (reg_idx == 0) return 0;
    // EX: alu_result available (computed at cycle 1)
    if (EX_Stage.is_active && EX_Stage.reg_write && EX_Stage.dest_reg == reg_idx && !EX_Stage.mem_to_reg) {
        printf("  [FWD] R%d <- EX (PC %d) = %d\n", reg_idx, EX_Stage.instruction_address, EX_Stage.alu_result);
        return EX_Stage.alu_result;
    }
    // MEM: alu_result available, but NOT mem_read_data (LW not loaded yet)
    if (MEM_Stage.is_active && MEM_Stage.reg_write && MEM_Stage.dest_reg == reg_idx && !MEM_Stage.mem_to_reg) {
        printf("  [FWD] R%d <- MEM (PC %d) = %d\n", reg_idx, MEM_Stage.instruction_address, MEM_Stage.alu_result);
        return MEM_Stage.alu_result;
    }
    // WB: both alu_result and mem_read_data available
    if (WB_Stage.is_active && WB_Stage.reg_write && WB_Stage.dest_reg == reg_idx) {
        int32_t val = WB_Stage.mem_to_reg ? WB_Stage.mem_read_data : WB_Stage.alu_result;
        printf("  [FWD] R%d <- WB (PC %d) = %d\n", reg_idx, WB_Stage.instruction_address, val);
        return val;
    }
    return default_val;
}

static void get_source_regs(int32_t raw, int *src1, int *src2) {
    int opcode = (raw >> 28) & 0xF;
    int r1     = (raw >> 23) & 0x1F;
    int r2     = (raw >> 18) & 0x1F;
    *src1 = -1;
    *src2 = -1;
    switch (opcode) {
        case OP_ADD: case OP_SUB: case OP_BNE: case OP_SW:
            *src1 = r1; *src2 = r2; break;
        case OP_MULI: case OP_ADDI: case OP_ANDI: case OP_XORI:
        case OP_SLL: case OP_SRL: case OP_LW:
            *src1 = r2; break;
        case OP_J: break;
    }
}

/* Only stall for load-use: LW in EX or MEM hasn't loaded data yet */
int check_load_use_hazard() {
    if (!ID_Stage.is_active) return 0;
    int src1, src2;
    get_source_regs(ID_Stage.raw_instruction, &src1, &src2);
    if (src1 == -1 && src2 == -1) return 0;
    InstructionContext *stages[] = { &EX_Stage, &MEM_Stage };
    for (int i = 0; i < 2; i++) {
        if (!stages[i]->is_active || !stages[i]->reg_write || !stages[i]->mem_to_reg) continue;
        int dest = stages[i]->dest_reg;
        if (dest <= 0) continue;
        if (dest == src1 || dest == src2) {
            printf("  [STALL] Load-use: LW at PC %d -> R%d not ready\n", stages[i]->instruction_address, dest);
            return 1;
        }
    }
    return 0;
}

// ==========================================
// STAGE IMPLEMENTATIONS
// ==========================================
void Fetch() {
    // Use instruction_count (set by parser) instead of checking for zero.
    // This avoids misinterpreting ADD R0 R0 R0 (encoded as 0x00000000) as end-of-program.
    if (PC >= instruction_count) {
        PC = 1024; // Trigger end condition
        IF_Stage.is_active = 0; // Don't pass an empty instruction down
        return;
    }
    IF_Stage.instruction_address = PC;
    IF_Stage.raw_instruction = memory[PC];
    printf("  [IF] Fetched instruction 0x%08X at PC %d\n", IF_Stage.raw_instruction, PC);
    PC++; // Increment PC
}

void Decode() { 
    if (ID_Stage.cycles_remaining == 1) {
        int32_t inst = ID_Stage.raw_instruction;
        
        // Parse all fields regardless of instruction format
        ID_Stage.opcode = (inst >> 28) & 0xF;
        ID_Stage.r1     = (inst >> 23) & 0x1F;
        ID_Stage.r2     = (inst >> 18) & 0x1F;
        ID_Stage.r3     = (inst >> 13) & 0x1F;
        ID_Stage.shamt  = inst & 0x1FFF;  // SHAMT is 13 bits [12:0]
        
        // Immediate (18 bits, sign extended)
        int32_t imm = inst & 0x3FFFF;
        if (imm & 0x20000) {
            imm |= 0xFFFC0000;
        }
        ID_Stage.imm = imm;
        
        // Address (28 bits)
        ID_Stage.address = inst & 0xFFFFFFF;
        
        // Read values from registers, then apply forwarding
        ID_Stage.val_r1 = forward_value(ID_Stage.r1, registers[ID_Stage.r1]);
        ID_Stage.val_r2 = forward_value(ID_Stage.r2, registers[ID_Stage.r2]);
        ID_Stage.val_r3 = forward_value(ID_Stage.r3, registers[ID_Stage.r3]);
        
        // Default control signals
        ID_Stage.reg_write  = 0;
        ID_Stage.mem_read   = 0;
        ID_Stage.mem_write  = 0;
        ID_Stage.mem_to_reg = 0;
        ID_Stage.dest_reg   = -1;
        
        switch (ID_Stage.opcode) {
            case 0: // ADD
            case 1: // SUB
                ID_Stage.reg_write = 1;
                ID_Stage.dest_reg = ID_Stage.r3; // R3 is the destination for ADD/SUB!
                break;

            case 8: // SLL
            case 9: // SRL
            case 2: // MULI
            case 3: // ADDI
            case 5: // ANDI
            case 6: // XORI
                ID_Stage.reg_write = 1;
                ID_Stage.dest_reg = ID_Stage.r1; // R1 is the destination for these
                break;
                
            case 10: // LW
                ID_Stage.reg_write = 1;
                ID_Stage.mem_read = 1;
                ID_Stage.mem_to_reg = 1;
                ID_Stage.dest_reg = ID_Stage.r1;
                break;
                
            case 11: // SW
                ID_Stage.mem_write = 1;
                break;
                
            case 4: // BNE
            case 7: // J
                // No WB, no MEM read/write
                break;
        }
        
        printf("  [ID] Cycle 1: Decoded Instruction at PC %d: Opcode=%d, R1=%d, R2=%d, R3=%d, Imm=%d\n", 
               ID_Stage.instruction_address, ID_Stage.opcode, ID_Stage.r1, ID_Stage.r2, ID_Stage.r3, ID_Stage.imm);
    } else if (ID_Stage.cycles_remaining == 2) {
        printf("  [ID] Cycle 2: Decode complete for instruction at PC %d.\n", ID_Stage.instruction_address);
    }
}

void Execute() {
    execute_alu();     // ALU logic lives in alu.c
    execute_branch();  // Branch/Jump logic lives in branch.c
}

void MemoryAccess() {
    if (!MEM_Stage.is_active) return;

    if (MEM_Stage.mem_read) {
        /*
         * LW R1, R2, IMM
         * R1 = Memory[R2 + IMM]
         *
         * The effective address is computed from the register value
         * read during Decode (val_r2) plus the sign-extended immediate.
         * Data memory spans addresses 1024-2047.
         */
        int32_t effective_addr = MEM_Stage.val_r2 + MEM_Stage.imm;

        if (effective_addr >= 0 && effective_addr < 2048) {
            MEM_Stage.mem_read_data = memory[effective_addr];
            if (effective_addr < DATA_START)
                printf("  [MEM] LW WARNING: Reading from instruction segment [%d]\n", effective_addr);
            printf("  [MEM] LW: Read memory[%d] = %d\n",
                   effective_addr, MEM_Stage.mem_read_data);
        } else {
            printf("  [MEM] LW: ERROR - Address %d out of bounds!\n", effective_addr);
            MEM_Stage.mem_read_data = 0;
        }
    }
    else if (MEM_Stage.mem_write) {
        /*
         * SW R1, R2, IMM
         * Memory[R2 + IMM] = R1
         *
         * val_r1 holds the value of R1 (the data to store),
         * val_r2 holds the value of R2 (base address),
         * imm is the offset.
         * Data memory spans addresses 1024-2047.
         */
        int32_t effective_addr = MEM_Stage.val_r2 + MEM_Stage.imm;

        if (effective_addr >= 0 && effective_addr < 2048) {
            if (effective_addr < DATA_START)
                printf("  [MEM] SW WARNING: Writing to instruction segment [%d]\n", effective_addr);
            memory[effective_addr] = MEM_Stage.val_r1;
            printf("  [MEM] SW: memory[%d] = %d\n",
                   effective_addr, MEM_Stage.val_r1);
        } else {
            printf("  [MEM] SW: ERROR - Address %d out of bounds!\n", effective_addr);
        }
    }
    else {
        /* Non-memory instruction: pass through */
        printf("  [MEM] Pass-through (no memory access) for instruction at PC %d\n",
               MEM_Stage.instruction_address);
    }
}

void WriteBack() {
    if (!WB_Stage.is_active) return;

    if (WB_Stage.reg_write) {
        /*
         * Determine the value to write:
         *   - If mem_to_reg == 1 (LW), write the data loaded from memory.
         *   - Otherwise, write the ALU result.
         */
        int32_t write_value = WB_Stage.mem_to_reg ? WB_Stage.mem_read_data : WB_Stage.alu_result;
        int dest = WB_Stage.dest_reg;

        if (dest > 0 && dest < 32) {
            /* Normal register write */
            registers[dest] = write_value;
            printf("  [WB] R%d = %d\n", dest, write_value);
        } else if (dest == 0) {
            /*
             * R0 is hard-wired to 0. The instruction proceeds normally
             * through the pipeline, but the write is silently discarded.
             * (Project spec: "Don't throw an error, let it continue.")
             */
            printf("  [WB] Write to R0 discarded (R0 is hardwired to 0)\n");
        } else {
            printf("  [WB] No write-back (dest_reg = %d)\n", dest);
        }
    } else {
        /* Instructions like BNE, J, SW don't write back to registers */
        printf("  [WB] No register write for instruction at PC %d\n",
               WB_Stage.instruction_address);
    }
}

/* ==========================================
 * PRINT: Per-cycle pipeline state
 * Required by project spec (Printings section):
 *   a) Clock cycle number
 *   b) Which instruction is in each stage + input values
 *   c) Register updates
 *   d) Memory updates
 * ========================================== */
void PrintCycleState() {
    printf("\n--- Cycle %d ---\n", clock_cycle);

    /* Pipeline overview */
    printf("  Pipeline: ");
    if (IF_Stage.is_active)
        printf("[IF: PC=%d] ", IF_Stage.instruction_address);
    if (ID_Stage.is_active)
        printf("[ID: PC=%d Cyc=%d/2] ", ID_Stage.instruction_address, ID_Stage.cycles_remaining);
    if (EX_Stage.is_active)
        printf("[EX: PC=%d Cyc=%d/2] ", EX_Stage.instruction_address, EX_Stage.cycles_remaining);
    if (MEM_Stage.is_active)
        printf("[MEM: PC=%d] ", MEM_Stage.instruction_address);
    if (WB_Stage.is_active)
        printf("[WB: PC=%d] ", WB_Stage.instruction_address);
    if (!IF_Stage.is_active && !ID_Stage.is_active && !EX_Stage.is_active &&
        !MEM_Stage.is_active && !WB_Stage.is_active)
        printf("(all stages empty)");
    printf("\n");

    /* PC value */
    printf("  PC = %d\n", PC);

    /* Non-zero registers */
    printf("  Registers: ");
    int any_nonzero = 0;
    for (int i = 0; i < 32; i++) {
        if (registers[i] != 0) {
            printf("R%d=%d  ", i, registers[i]);
            any_nonzero = 1;
        }
    }
    if (!any_nonzero) printf("(all zero)");
    printf("\n");
}

/* ==========================================
 * PRINT: Final state after simulation ends
 * Required by project spec:
 *   d) Content of all registers after last cycle
 *   e) Full content of memory
 * ========================================== */
void PrintFinalState() {
    printf("\n========================================\n");
    printf("  Final Register State\n");
    printf("========================================\n");
    for (int i = 0; i < 32; i++) {
        printf("  R%-2d = %d\n", i, registers[i]);
    }
    printf("  PC  = %d\n", PC);

    printf("\n========================================\n");
    printf("  Instruction Memory (non-zero)\n");
    printf("========================================\n");
    for (int i = 0; i < 1024; i++) {
        if (memory[i] != 0) {
            printf("  [%4d] = 0x%08X  (%d)\n", i, (unsigned)memory[i], memory[i]);
        }
    }

    printf("\n========================================\n");
    printf("  Data Memory (non-zero)\n");
    printf("========================================\n");
    for (int i = 1024; i < 2048; i++) {
        if (memory[i] != 0) {
            printf("  [%4d] = %d\n", i, memory[i]);
        }
    }
    printf("\n");
}