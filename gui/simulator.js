/* ═══════════════════════════════════════════════════════════════
   simulator.js — Pipeline Simulator Engine (JavaScript)
   
   A faithful JavaScript reimplementation of the C pipeline simulator
   from Package 1: Spicy Von Neumann Fillet (CSEN 601).
   
   Mirrors: pipeline_simulator.c, alu.c, branch.c, parser.c, globals.c
   ═══════════════════════════════════════════════════════════════ */

const Simulator = (() => {

  /* ── Opcodes ──────────────────────────────────────────────── */
  const OP = {
    ADD: 0, SUB: 1, MULI: 2, ADDI: 3,
    BNE: 4, ANDI: 5, XORI: 6, J: 7,
    SLL: 8, SRL: 9, LW: 10, SW: 11
  };

  const OPCODE_NAMES = ['ADD','SUB','MULI','ADDI','BNE','ANDI','XORI','J','SLL','SRL','LW','SW'];

  const FORMAT_R = 0, FORMAT_I = 1, FORMAT_J = 2;

  function getFormat(opcode) {
    switch (opcode) {
      case OP.ADD: case OP.SUB: case OP.SLL: case OP.SRL: return FORMAT_R;
      case OP.MULI: case OP.ADDI: case OP.BNE: case OP.ANDI: case OP.XORI:
      case OP.LW: case OP.SW: return FORMAT_I;
      case OP.J: return FORMAT_J;
      default: return -1;
    }
  }

  function opName(opcode) {
    return OPCODE_NAMES[opcode] || '???';
  }

  /* ── Create empty instruction context ─────────────────────── */
  function emptyCtx() {
    return {
      is_active: 0,
      cycles_remaining: 0,
      instruction_address: 0,
      raw_instruction: 0,
      opcode: 0, r1: 0, r2: 0, r3: 0,
      shamt: 0, imm: 0, address: 0,
      val_r1: 0, val_r2: 0, val_r3: 0,
      dest_reg: -1,
      alu_result: 0,
      branch_taken: 0,
      branch_target: 0,
      mem_read_data: 0,
      reg_write: 0, mem_read: 0, mem_write: 0, mem_to_reg: 0
    };
  }

  /* ── Simulator State ──────────────────────────────────────── */
  let memory = new Int32Array(2048);
  let registers = new Int32Array(32);
  let PC = 0;
  let clock_cycle = 0;
  let instruction_count = 0;
  let program_done = false;
  let log = [];

  let IF_Stage  = emptyCtx();
  let ID_Stage  = emptyCtx();
  let EX_Stage  = emptyCtx();
  let MEM_Stage = emptyCtx();
  let WB_Stage  = emptyCtx();

  // Track which instructions are loaded (for timeline display)
  let instructionTexts = [];

  // Timeline data: timeline[instrIndex] = { stages: { cycle: stageName } }
  let timeline = {};

  /* ── Logging helper ───────────────────────────────────────── */
  function addLog(msg, type) {
    log.push({ msg, type: type || 'info' });
  }

  /* ── Parser (mirrors parser.c) ────────────────────────────── */
  function parseRegister(s) {
    s = s.trim().replace(/,/g, '');
    if (s[0] === 'R' || s[0] === 'r') return parseInt(s.substring(1), 10);
    return parseInt(s, 10);
  }

  function mnemonicToOpcode(m) {
    m = m.toUpperCase();
    const idx = OPCODE_NAMES.indexOf(m);
    return idx >= 0 ? idx : -1;
  }

  function encodeR(op, r1, r2, r3, shamt) {
    let w = 0;
    w |= (op & 0xF) << 28;
    w |= (r1 & 0x1F) << 23;
    w |= (r2 & 0x1F) << 18;
    w |= (r3 & 0x1F) << 13;
    w |= (shamt & 0x1FFF);
    return w | 0; // force int32
  }

  function encodeI(op, r1, r2, imm) {
    let w = 0;
    w |= (op & 0xF) << 28;
    w |= (r1 & 0x1F) << 23;
    w |= (r2 & 0x1F) << 18;
    w |= (imm & 0x3FFFF);
    return w | 0;
  }

  function encodeJ(op, addr) {
    let w = 0;
    w |= (op & 0xF) << 28;
    w |= (addr & 0x0FFFFFFF);
    return w | 0;
  }

  function parseProgram(text) {
    const lines = text.split('\n');
    let addr = 0;
    instructionTexts = [];

    for (const rawLine of lines) {
      if (addr > 1023) break;
      const line = rawLine.trim();
      if (!line || line[0] === '#' || line[0] === ';') continue;

      const tokens = line.split(/\s+/);
      if (tokens.length < 1) continue;

      const mnemonic = tokens[0];
      const opcode = mnemonicToOpcode(mnemonic);
      if (opcode < 0) {
        addLog(`WARNING: Unknown mnemonic '${mnemonic}' at address ${addr}`, 'hazard');
        continue;
      }

      const fmt = getFormat(opcode);
      let encoded = 0;

      switch (fmt) {
        case FORMAT_R:
          if (opcode === OP.SLL || opcode === OP.SRL) {
            encoded = encodeR(opcode, parseRegister(tokens[1]), parseRegister(tokens[2]), 0, parseInt(tokens[3], 10));
          } else {
            encoded = encodeR(opcode, parseRegister(tokens[1]), parseRegister(tokens[2]), parseRegister(tokens[3]), 0);
          }
          break;
        case FORMAT_I:
          encoded = encodeI(opcode, parseRegister(tokens[1]), parseRegister(tokens[2]), parseInt(tokens[3], 10));
          break;
        case FORMAT_J:
          encoded = encodeJ(opcode, parseInt(tokens[1], 10));
          break;
      }

      memory[addr] = encoded;
      instructionTexts.push(line);
      addLog(`[PARSER] Addr ${addr}: ${line} => 0x${(encoded >>> 0).toString(16).padStart(8, '0')}`, 'info');
      addr++;
    }

    instruction_count = addr;
    addLog(`[PARSER] Loaded ${instruction_count} instructions.`, 'info');
    return instruction_count;
  }

  /* ── Fetch ────────────────────────────────────────────────── */
  function Fetch() {
    if (PC >= instruction_count) {
      PC = 1024;
      IF_Stage.is_active = 0;
      return;
    }
    IF_Stage.instruction_address = PC;
    IF_Stage.raw_instruction = memory[PC];
    addLog(`[IF] Fetched 0x${(memory[PC] >>> 0).toString(16).padStart(8, '0')} at PC ${PC}`, 'fetch');

    // Timeline
    if (PC < instruction_count) {
      if (!timeline[PC]) timeline[PC] = {};
      timeline[PC][clock_cycle] = 'IF';
    }

    PC++;
  }

  /* ── Decode (mirrors pipeline_simulator.c Decode()) ───────── */
  function Decode() {
    const instrAddr = ID_Stage.instruction_address;

    if (ID_Stage.cycles_remaining === 1) {
      const inst = ID_Stage.raw_instruction;

      ID_Stage.opcode  = (inst >>> 28) & 0xF;
      ID_Stage.r1      = (inst >>> 23) & 0x1F;
      ID_Stage.r2      = (inst >>> 18) & 0x1F;
      ID_Stage.r3      = (inst >>> 13) & 0x1F;
      ID_Stage.shamt   = inst & 0x1FFF;

      let imm = inst & 0x3FFFF;
      if (imm & 0x20000) imm |= 0xFFFC0000;
      ID_Stage.imm = imm | 0;

      ID_Stage.address = inst & 0xFFFFFFF;

      ID_Stage.val_r1 = registers[ID_Stage.r1];
      ID_Stage.val_r2 = registers[ID_Stage.r2];
      ID_Stage.val_r3 = registers[ID_Stage.r3];

      ID_Stage.reg_write = 0;
      ID_Stage.mem_read = 0;
      ID_Stage.mem_write = 0;
      ID_Stage.mem_to_reg = 0;
      ID_Stage.dest_reg = -1;

      switch (ID_Stage.opcode) {
        case OP.ADD: case OP.SUB:
          ID_Stage.reg_write = 1;
          ID_Stage.dest_reg = ID_Stage.r3;
          break;
        case OP.SLL: case OP.SRL: case OP.MULI: case OP.ADDI:
        case OP.ANDI: case OP.XORI:
          ID_Stage.reg_write = 1;
          ID_Stage.dest_reg = ID_Stage.r1;
          break;
        case OP.LW:
          ID_Stage.reg_write = 1;
          ID_Stage.mem_read = 1;
          ID_Stage.mem_to_reg = 1;
          ID_Stage.dest_reg = ID_Stage.r1;
          break;
        case OP.SW:
          ID_Stage.mem_write = 1;
          break;
      }

      addLog(`[ID] Cycle 1: Op=${opName(ID_Stage.opcode)} R1=R${ID_Stage.r1} R2=R${ID_Stage.r2} R3=R${ID_Stage.r3} Imm=${ID_Stage.imm}`, 'decode');
    } else if (ID_Stage.cycles_remaining === 2) {
      addLog(`[ID] Cycle 2: Decode complete for PC ${instrAddr}`, 'decode');
    }

    // Timeline
    if (instrAddr < instruction_count) {
      if (!timeline[instrAddr]) timeline[instrAddr] = {};
      timeline[instrAddr][clock_cycle] = 'ID';
    }
  }

  /* ── Execute ALU (mirrors alu.c) ──────────────────────────── */
  function executeALU() {
    if (!EX_Stage.is_active) return;
    const opcode = EX_Stage.opcode;

    if (EX_Stage.cycles_remaining === 1) {
      const val_r2 = EX_Stage.val_r2;
      const imm = EX_Stage.imm;
      const shamt = EX_Stage.shamt;
      let result = 0;

      switch (opcode) {
        case OP.ADD:
          result = (EX_Stage.val_r1 + val_r2) | 0;
          addLog(`[EX] ADD: ${EX_Stage.val_r1} + ${val_r2} = ${result}`, 'execute');
          break;
        case OP.SUB:
          result = (EX_Stage.val_r1 - val_r2) | 0;
          addLog(`[EX] SUB: ${EX_Stage.val_r1} - ${val_r2} = ${result}`, 'execute');
          break;
        case OP.MULI:
          result = Math.imul(val_r2, imm);
          addLog(`[EX] MULI: ${val_r2} * ${imm} = ${result}`, 'execute');
          break;
        case OP.ADDI:
          result = (val_r2 + imm) | 0;
          addLog(`[EX] ADDI: ${val_r2} + ${imm} = ${result}`, 'execute');
          break;
        case OP.ANDI:
          result = val_r2 & imm;
          addLog(`[EX] ANDI: ${val_r2} & ${imm} = ${result}`, 'execute');
          break;
        case OP.XORI:
          result = val_r2 ^ imm;
          addLog(`[EX] XORI: ${val_r2} ^ ${imm} = ${result}`, 'execute');
          break;
        case OP.SLL:
          result = (val_r2 << shamt) | 0;
          addLog(`[EX] SLL: ${val_r2} << ${shamt} = ${result}`, 'execute');
          break;
        case OP.SRL:
          result = (val_r2 >>> shamt) | 0;
          addLog(`[EX] SRL: ${val_r2} >>> ${shamt} = ${result}`, 'execute');
          break;
        default:
          break;
      }
      EX_Stage.alu_result = result;
      addLog(`[EX] ALU Cycle 1/2 done for PC ${EX_Stage.instruction_address}`, 'execute');

    } else if (EX_Stage.cycles_remaining === 2) {
      addLog(`[EX] ALU Cycle 2/2 done for PC ${EX_Stage.instruction_address}. Result = ${EX_Stage.alu_result}`, 'execute');
    }
  }

  /* ── Execute Branch (mirrors branch.c) ────────────────────── */
  function executeBranch() {
    if (!EX_Stage.is_active) return;
    const opcode = EX_Stage.opcode;
    if (opcode !== OP.BNE && opcode !== OP.J) return;

    if (EX_Stage.cycles_remaining === 1) {
      switch (opcode) {
        case OP.BNE:
          if (EX_Stage.val_r1 !== EX_Stage.val_r2) {
            EX_Stage.branch_taken = 1;
            EX_Stage.branch_target = EX_Stage.instruction_address + 1 + EX_Stage.imm;
            addLog(`[EX] BNE: R${EX_Stage.r1}(${EX_Stage.val_r1}) != R${EX_Stage.r2}(${EX_Stage.val_r2}) -> TAKEN, target=${EX_Stage.branch_target}`, 'hazard');
          } else {
            EX_Stage.branch_taken = 0;
            addLog(`[EX] BNE: R${EX_Stage.r1}(${EX_Stage.val_r1}) == R${EX_Stage.r2}(${EX_Stage.val_r2}) -> NOT TAKEN`, 'execute');
          }
          break;
        case OP.J:
          EX_Stage.branch_taken = 1;
          EX_Stage.branch_target = (EX_Stage.instruction_address & 0xF0000000) | EX_Stage.address;
          addLog(`[EX] J: Unconditional jump -> target=${EX_Stage.branch_target}`, 'hazard');
          break;
      }
    } else if (EX_Stage.cycles_remaining === 2) {
      addLog(`[EX] Branch Cycle 2/2 done. taken=${EX_Stage.branch_taken}, target=${EX_Stage.branch_target}`, 'execute');
    }
  }

  function Execute() {
    executeALU();
    executeBranch();

    // Timeline
    const instrAddr = EX_Stage.instruction_address;
    if (EX_Stage.is_active && instrAddr < instruction_count) {
      if (!timeline[instrAddr]) timeline[instrAddr] = {};
      timeline[instrAddr][clock_cycle] = 'EX';
    }
  }

  /* ── Memory Access ────────────────────────────────────────── */
  function MemoryAccess() {
    if (!MEM_Stage.is_active) return;

    if (MEM_Stage.mem_read) {
      // LW: read from memory[R2 + IMM]
      const addr = (MEM_Stage.val_r2 + MEM_Stage.imm) | 0;
      if (addr >= 0 && addr < 2048) {
        MEM_Stage.mem_read_data = memory[addr];
        addLog(`[MEM] LW: Read memory[${addr}] = ${memory[addr]}`, 'memory');
      } else {
        addLog(`[MEM] LW: Address ${addr} out of bounds!`, 'hazard');
      }
    }

    if (MEM_Stage.mem_write) {
      // SW: write val_r1 to memory[R2 + IMM]
      const addr = (MEM_Stage.val_r2 + MEM_Stage.imm) | 0;
      if (addr >= 0 && addr < 2048) {
        memory[addr] = MEM_Stage.val_r1;
        addLog(`[MEM] SW: memory[${addr}] = ${MEM_Stage.val_r1}`, 'memory');
      } else {
        addLog(`[MEM] SW: Address ${addr} out of bounds!`, 'hazard');
      }
    }

    if (!MEM_Stage.mem_read && !MEM_Stage.mem_write) {
      addLog(`[MEM] Pass-through for ${opName(MEM_Stage.opcode)} at PC ${MEM_Stage.instruction_address}`, 'memory');
    }

    // Timeline
    const instrAddr = MEM_Stage.instruction_address;
    if (instrAddr < instruction_count) {
      if (!timeline[instrAddr]) timeline[instrAddr] = {};
      timeline[instrAddr][clock_cycle] = 'MEM';
    }
  }

  /* ── Write Back ───────────────────────────────────────────── */
  function WriteBack() {
    if (!WB_Stage.is_active) return;

    if (WB_Stage.reg_write && WB_Stage.dest_reg > 0 && WB_Stage.dest_reg < 32) {
      const value = WB_Stage.mem_to_reg ? WB_Stage.mem_read_data : WB_Stage.alu_result;
      registers[WB_Stage.dest_reg] = value;
      addLog(`[WB] R${WB_Stage.dest_reg} = ${value}  (${opName(WB_Stage.opcode)})`, 'wb');
    } else {
      addLog(`[WB] No write-back for ${opName(WB_Stage.opcode)} at PC ${WB_Stage.instruction_address}`, 'wb');
    }

    // Timeline
    const instrAddr = WB_Stage.instruction_address;
    if (instrAddr < instruction_count) {
      if (!timeline[instrAddr]) timeline[instrAddr] = {};
      timeline[instrAddr][clock_cycle] = 'WB';
    }
  }

  /* ── Data Hazard Check (mirrors C stalling logic) ──────────── */
  function getSourceRegs(raw) {
    const opcode = (raw >>> 28) & 0xF;
    const r1 = (raw >>> 23) & 0x1F;
    const r2 = (raw >>> 18) & 0x1F;
    let src1 = -1, src2 = -1;
    switch (opcode) {
      case OP.ADD: case OP.SUB: case OP.BNE: case OP.SW:
        src1 = r1; src2 = r2; break;
      case OP.MULI: case OP.ADDI: case OP.ANDI: case OP.XORI:
      case OP.SLL: case OP.SRL: case OP.LW:
        src1 = r2; break;
    }
    return { src1, src2 };
  }

  function checkDataHazard() {
    if (!ID_Stage.is_active) return false;
    const { src1, src2 } = getSourceRegs(ID_Stage.raw_instruction);
    if (src1 === -1 && src2 === -1) return false;
    const stages = [EX_Stage, MEM_Stage, WB_Stage];
    for (const s of stages) {
      if (!s.is_active || !s.reg_write) continue;
      if (s.dest_reg <= 0) continue;
      if (s.dest_reg === src1 || s.dest_reg === src2) {
        addLog(`[STALL] R${s.dest_reg} needed by PC ${ID_Stage.instruction_address}, waiting on PC ${s.instruction_address}`, 'hazard');
        return true;
      }
    }
    return false;
  }

  // Snapshot of IF for UI display (IF is transient — fetched & transferred in same cycle)
  let IF_Snapshot = emptyCtx();

  /* ── Main Clock Step (mirrors the while-loop in pipeline_simulator.c) ─── */
  function step() {
    if (program_done) return false;

    clock_cycle++;
    IF_Snapshot = emptyCtx(); // reset snapshot each cycle
    addLog(`━━━ Cycle ${clock_cycle} ━━━`, 'cycle');

    // Check termination
    if (PC >= instruction_count &&
        !IF_Stage.is_active && !ID_Stage.is_active &&
        !EX_Stage.is_active && !MEM_Stage.is_active && !WB_Stage.is_active) {
      program_done = true;
      addLog(`Simulation complete in ${clock_cycle - 1} cycles.`, 'info');
      return false;
    }

    // 1. WRITE BACK
    if (WB_Stage.is_active) {
      // Hazard handling: flush on branch taken
      if (WB_Stage.branch_taken) {
        PC = WB_Stage.branch_target;
        addLog(`[HAZARD] Branch taken! Flushing EX and ID. PC -> ${PC}`, 'hazard');

        // Mark flushed in timeline
        if (EX_Stage.is_active && EX_Stage.instruction_address < instruction_count) {
          if (!timeline[EX_Stage.instruction_address]) timeline[EX_Stage.instruction_address] = {};
          timeline[EX_Stage.instruction_address][clock_cycle] = 'FL';
        }
        if (ID_Stage.is_active && ID_Stage.instruction_address < instruction_count) {
          if (!timeline[ID_Stage.instruction_address]) timeline[ID_Stage.instruction_address] = {};
          timeline[ID_Stage.instruction_address][clock_cycle] = 'FL';
        }

        EX_Stage = emptyCtx();
        ID_Stage = emptyCtx();
      }
      WriteBack();
      WB_Stage = emptyCtx();
    }

    // 2. MEMORY
    if (MEM_Stage.is_active) {
      MemoryAccess();
      WB_Stage = { ...MEM_Stage };
      MEM_Stage = emptyCtx();
    }

    // 3. EXECUTE (2 cycles)
    if (EX_Stage.is_active) {
      EX_Stage.cycles_remaining++;
      Execute();
      if (EX_Stage.cycles_remaining === 2) {
        MEM_Stage = { ...EX_Stage };
        MEM_Stage.cycles_remaining = 0;
        EX_Stage = emptyCtx();
      }
    }

    // 4. DECODE (2 cycles) — with data hazard stalling
    let stalled = false;
    if (ID_Stage.is_active) {
      if (ID_Stage.cycles_remaining === 0 && checkDataHazard()) {
        stalled = true;
      } else {
        ID_Stage.cycles_remaining++;
        Decode();
        if (ID_Stage.cycles_remaining === 2) {
          EX_Stage = { ...ID_Stage };
          EX_Stage.cycles_remaining = 0;
          ID_Stage = emptyCtx();
        }
      }
    }

    // 5. FETCH (odd cycles only, not during a stall)
    if (!stalled && clock_cycle % 2 !== 0 && PC >= 0 && PC < instruction_count) {
      IF_Stage.is_active = 1;
      Fetch();
      if (IF_Stage.is_active) {
        IF_Snapshot = { ...IF_Stage };  // <-- save for UI
        ID_Stage = { ...IF_Stage };
        IF_Stage = emptyCtx();
      }
    }

    // Enforce R0 = 0
    registers[0] = 0;

    return true;
  }

  /* ── Public API ───────────────────────────────────────────── */
  function reset() {
    memory = new Int32Array(2048);
    registers = new Int32Array(32);
    PC = 0;
    clock_cycle = 0;
    instruction_count = 0;
    program_done = false;
    log = [];
    IF_Stage = emptyCtx();
    IF_Snapshot = emptyCtx();
    ID_Stage = emptyCtx();
    EX_Stage = emptyCtx();
    MEM_Stage = emptyCtx();
    WB_Stage = emptyCtx();
    instructionTexts = [];
    timeline = {};
  }

  function loadProgram(text) {
    reset();
    parseProgram(text);
  }

  function getState() {
    return {
      clock_cycle,
      PC,
      program_done,
      instruction_count,
      memory,
      registers: Array.from(registers),
      IF: { ...IF_Snapshot },
      ID: { ...ID_Stage },
      EX: { ...EX_Stage },
      MEM: { ...MEM_Stage },
      WB: { ...WB_Stage },
      log: [...log],
      timeline: JSON.parse(JSON.stringify(timeline)),
      instructionTexts: [...instructionTexts]
    };
  }

  function consumeLog() {
    const entries = [...log];
    log = [];
    return entries;
  }

  return {
    OP, OPCODE_NAMES, opName, getFormat, FORMAT_R, FORMAT_I, FORMAT_J,
    reset, loadProgram, step, getState, consumeLog
  };

})();
