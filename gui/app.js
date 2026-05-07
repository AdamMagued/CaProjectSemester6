/* ═══════════════════════════════════════════════════════════════
   app.js — UI Controller for Pipeline Simulator GUI
   
   Connects the Simulator engine to the DOM.
   Handles: theme toggle, editor line numbers, controls,
            pipeline stage display, register/memory views,
            timeline table, and log console.
   ═══════════════════════════════════════════════════════════════ */

(() => {
  'use strict';

  /* ── DOM References ────────────────────────────────────────── */
  const $  = (sel) => document.querySelector(sel);
  const $$ = (sel) => document.querySelectorAll(sel);

  const codeEditor   = $('#codeEditor');
  const lineNumbers  = $('#lineNumbers');
  const cycleNum     = $('#cycleNum');
  const statusInd    = $('#statusIndicator');
  const statusText   = $('#statusText');
  const logConsole   = $('#logConsole');
  const registerGrid = $('#registerGrid');
  const memoryGrid   = $('#memoryGrid');
  const timelineBody = $('#timelineBody');
  const timelineHeader = $('#timelineHeader');
  const timelineScroll = $('#timelineScroll');

  /* ── Theme Toggle ──────────────────────────────────────────── */
  const themeToggle = $('#themeToggle');
  const savedTheme = localStorage.getItem('pipeline-theme') || 'dark';
  document.documentElement.setAttribute('data-theme', savedTheme);

  themeToggle.addEventListener('click', () => {
    const current = document.documentElement.getAttribute('data-theme');
    const next = current === 'dark' ? 'light' : 'dark';
    document.documentElement.setAttribute('data-theme', next);
    localStorage.setItem('pipeline-theme', next);
  });

  /* ── Line Numbers ──────────────────────────────────────────── */
  function updateLineNumbers() {
    const lines = codeEditor.value.split('\n').length;
    let html = '';
    for (let i = 1; i <= Math.max(lines, 12); i++) {
      html += `<span>${i}</span>`;
    }
    lineNumbers.innerHTML = html;
  }

  codeEditor.addEventListener('input', updateLineNumbers);
  codeEditor.addEventListener('scroll', () => {
    lineNumbers.scrollTop = codeEditor.scrollTop;
  });
  updateLineNumbers();

  /* ── Register Grid Init ────────────────────────────────────── */
  function initRegisters() {
    let html = '';
    for (let i = 0; i < 32; i++) {
      html += `<div class="reg-cell" id="reg-${i}">
        <span class="reg-name">R${i}</span>
        <span class="reg-value" id="reg-val-${i}">0</span>
      </div>`;
    }
    registerGrid.innerHTML = html;
  }
  initRegisters();

  /* ── Memory Grid ────────────────────────────────────────────── */
  let memSegment = 'instr'; // 'instr' or 'data'

  function renderMemory() {
    const state = Simulator.getState();
    const start = memSegment === 'instr' ? 0 : 1024;
    const end = memSegment === 'instr' ? 1024 : 2048;
    let html = '';

    for (let i = start; i < end; i++) {
      const val = state.memory[i];
      if (val === 0 && i > state.instruction_count && memSegment === 'instr') continue;
      if (val === 0 && memSegment === 'data') continue;

      const hasVal = val !== 0;
      let decoded = '';
      if (hasVal && memSegment === 'instr') {
        const op = (val >>> 28) & 0xF;
        decoded = Simulator.opName(op);
      }

      html += `<div class="mem-row${hasVal ? ' has-value' : ''}">
        <span class="mem-addr">[${i}]</span>
        <span class="mem-decoded">${decoded}</span>
        <span class="mem-val">0x${(val >>> 0).toString(16).padStart(8, '0')}</span>
      </div>`;
    }

    if (!html) {
      html = '<div class="mem-row"><span class="mem-addr" style="color:var(--text-muted);font-family:var(--font-sans)">No non-zero entries</span></div>';
    }

    memoryGrid.innerHTML = html;
  }

  // Show all instruction slots initially
  function renderMemoryFull() {
    const state = Simulator.getState();
    const start = memSegment === 'instr' ? 0 : 1024;
    const end = memSegment === 'instr' ? Math.max(start + 12, state.instruction_count) : 1024 + 12;
    let html = '';

    for (let i = start; i < end; i++) {
      const val = state.memory[i];
      const hasVal = val !== 0;
      let decoded = '';
      if (hasVal && memSegment === 'instr' && i < state.instruction_count) {
        decoded = state.instructionTexts[i] || Simulator.opName((val >>> 28) & 0xF);
      }

      html += `<div class="mem-row${hasVal ? ' has-value' : ''}">
        <span class="mem-addr">[${i}]</span>
        <span class="mem-decoded">${decoded}</span>
        <span class="mem-val">${hasVal ? '0x' + (val >>> 0).toString(16).padStart(8, '0') : '0x00000000'}</span>
      </div>`;
    }

    memoryGrid.innerHTML = html;
  }

  $('#memSegInstr').addEventListener('click', () => {
    memSegment = 'instr';
    $('#memSegInstr').classList.add('active');
    $('#memSegData').classList.remove('active');
    renderMemoryFull();
  });

  $('#memSegData').addEventListener('click', () => {
    memSegment = 'data';
    $('#memSegData').classList.add('active');
    $('#memSegInstr').classList.remove('active');
    renderMemory();
  });

  $('#memJumpAddr').addEventListener('change', (e) => {
    const addr = parseInt(e.target.value, 10);
    if (isNaN(addr)) return;
    if (addr >= 1024) {
      memSegment = 'data';
      $('#memSegData').classList.add('active');
      $('#memSegInstr').classList.remove('active');
    } else {
      memSegment = 'instr';
      $('#memSegInstr').classList.add('active');
      $('#memSegData').classList.remove('active');
    }
    renderMemory();
    // Scroll to the address
    const rows = memoryGrid.querySelectorAll('.mem-row');
    for (const row of rows) {
      if (row.querySelector('.mem-addr').textContent === `[${addr}]`) {
        row.scrollIntoView({ behavior: 'smooth', block: 'center' });
        row.style.background = 'var(--accent-soft)';
        setTimeout(() => row.style.background = '', 1500);
        break;
      }
    }
  });

  /* ── Tab Switching ──────────────────────────────────────────── */
  $$('.state-tab').forEach(tab => {
    tab.addEventListener('click', () => {
      $$('.state-tab').forEach(t => t.classList.remove('active'));
      $$('.state-content').forEach(c => c.classList.remove('active'));
      tab.classList.add('active');
      $(`#tab-${tab.dataset.tab}`).classList.add('active');

      if (tab.dataset.tab === 'memory') renderMemoryFull();
    });
  });

  /* ── Update Pipeline Display ────────────────────────────────── */
  let prevRegisters = new Array(32).fill(0);

  function updateUI() {
    const state = Simulator.getState();

    // Cycle counter
    cycleNum.textContent = state.clock_cycle;

    // Status
    if (state.program_done) {
      statusInd.className = 'status-indicator done';
      statusText.textContent = `Done (${state.clock_cycle} cycles)`;
    } else if (state.clock_cycle > 0) {
      statusInd.className = 'status-indicator running';
      statusText.textContent = 'Running';
    } else {
      statusInd.className = 'status-indicator';
      statusText.textContent = 'Idle';
    }

    // Pipeline stages
    updateStage('IF', state.IF, {
      'if-pc':  state.IF.is_active ? `${state.IF.instruction_address}` : '—',
      'if-raw': state.IF.is_active ? `0x${(state.IF.raw_instruction >>> 0).toString(16).padStart(8, '0')}` : '—',
    });

    updateStage('ID', state.ID, {
      'id-op':    state.ID.is_active ? Simulator.opName(state.ID.opcode) : '—',
      'id-r1':    state.ID.is_active ? `R${state.ID.r1}` : '—',
      'id-r2':    state.ID.is_active ? `R${state.ID.r2}` : '—',
      'id-r3':    state.ID.is_active ? formatR3Imm(state.ID) : '—',
      'id-cycle': state.ID.is_active ? `${state.ID.cycles_remaining}/2` : '—',
    });

    updateStage('EX', state.EX, {
      'ex-op':     state.EX.is_active ? Simulator.opName(state.EX.opcode) : '—',
      'ex-alu':    state.EX.is_active ? `${state.EX.alu_result}` : '—',
      'ex-branch': state.EX.is_active ? (state.EX.branch_taken ? `→ ${state.EX.branch_target}` : 'No') : '—',
      'ex-cycle':  state.EX.is_active ? `${state.EX.cycles_remaining}/2` : '—',
    });

    updateStage('MEM', state.MEM, {
      'mem-op':   state.MEM.is_active ? Simulator.opName(state.MEM.opcode) : '—',
      'mem-addr': state.MEM.is_active ? (state.MEM.mem_read || state.MEM.mem_write ? `${state.MEM.val_r2 + state.MEM.imm}` : '—') : '—',
      'mem-data': state.MEM.is_active ? (state.MEM.mem_read ? `${state.MEM.mem_read_data}` : state.MEM.mem_write ? `${state.MEM.val_r1}` : '—') : '—',
    });

    updateStage('WB', state.WB, {
      'wb-op':    state.WB.is_active ? Simulator.opName(state.WB.opcode) : '—',
      'wb-dest':  state.WB.is_active && state.WB.reg_write ? `R${state.WB.dest_reg}` : '—',
      'wb-value': state.WB.is_active && state.WB.reg_write ? `${state.WB.mem_to_reg ? state.WB.mem_read_data : state.WB.alu_result}` : '—',
    });

    // Registers
    for (let i = 0; i < 32; i++) {
      const el = $(`#reg-val-${i}`);
      const cell = $(`#reg-${i}`);
      el.textContent = state.registers[i];
      if (state.registers[i] !== prevRegisters[i]) {
        cell.classList.add('changed');
        setTimeout(() => cell.classList.remove('changed'), 800);
      }
    }
    prevRegisters = [...state.registers];

    // Log
    const newEntries = Simulator.consumeLog();
    for (const entry of newEntries) {
      const div = document.createElement('div');
      div.className = `log-entry log-entry--${entry.type}`;
      div.textContent = entry.msg;
      logConsole.appendChild(div);
    }
    logConsole.scrollTop = logConsole.scrollHeight;

    // Timeline
    updateTimeline(state);

    // Memory (if visible)
    if ($('#tab-memory').classList.contains('active')) {
      renderMemoryFull();
    }
  }

  function updateStage(id, stageData, fields) {
    const el = $(`#stage-${id}`);
    const statusEl = $(`#${id.toLowerCase()}-status`);

    if (stageData.is_active) {
      el.classList.add('active');
      el.classList.remove('flushed');
      statusEl.textContent = 'Active';
    } else {
      el.classList.remove('active');
      el.classList.remove('flushed');
      statusEl.textContent = 'Empty';
    }

    for (const [fieldId, value] of Object.entries(fields)) {
      const fieldEl = $(`#${fieldId}`);
      if (fieldEl) fieldEl.textContent = value;
    }
  }

  function formatR3Imm(stageData) {
    const fmt = Simulator.getFormat(stageData.opcode);
    if (fmt === Simulator.FORMAT_R) {
      return (stageData.opcode === Simulator.OP.ADD || stageData.opcode === Simulator.OP.SUB)
        ? `R${stageData.r3}`
        : `SHAMT=${stageData.shamt}`;
    }
    if (fmt === Simulator.FORMAT_I) return `IMM=${stageData.imm}`;
    if (fmt === Simulator.FORMAT_J) return `ADDR=${stageData.address}`;
    return '—';
  }

  /* ── Timeline Table ────────────────────────────────────────── */
  function updateTimeline(state) {
    const tl = state.timeline;
    const maxCycle = state.clock_cycle;
    const instrTexts = state.instructionTexts;

    // Header
    let headerHtml = '<th class="timeline-th timeline-th--instr">Instruction</th>';
    for (let c = 1; c <= maxCycle; c++) {
      headerHtml += `<th class="timeline-th">C${c}</th>`;
    }
    timelineHeader.innerHTML = headerHtml;

    // Body
    let bodyHtml = '';
    for (let i = 0; i < instrTexts.length; i++) {
      bodyHtml += `<tr><td>${instrTexts[i]}</td>`;
      for (let c = 1; c <= maxCycle; c++) {
        const stage = tl[i] && tl[i][c] ? tl[i][c] : '';
        bodyHtml += `<td class="${stage ? 'tl-' + stage : ''}">${stage}</td>`;
      }
      bodyHtml += '</tr>';
    }
    timelineBody.innerHTML = bodyHtml;

    // Auto-scroll right
    if (timelineScroll) {
      timelineScroll.scrollLeft = timelineScroll.scrollWidth;
    }
  }

  /* ── Controls ──────────────────────────────────────────────── */
  let runTimer = null;

  function loadAndReset() {
    const code = codeEditor.value.trim();
    if (!code) {
      addUILog('No program loaded. Enter assembly instructions first.', 'hazard');
      return false;
    }
    Simulator.loadProgram(code);
    updateUI();
    return true;
  }

  function addUILog(msg, type) {
    const div = document.createElement('div');
    div.className = `log-entry log-entry--${type || 'info'}`;
    div.textContent = msg;
    logConsole.appendChild(div);
    logConsole.scrollTop = logConsole.scrollHeight;
  }

  // Step button
  $('#btnStep').addEventListener('click', () => {
    if (runTimer) { clearInterval(runTimer); runTimer = null; }

    const state = Simulator.getState();
    if (state.clock_cycle === 0) {
      if (!loadAndReset()) return;
    }

    if (state.program_done) {
      addUILog('Program already finished. Press Reset to restart.', 'info');
      return;
    }

    Simulator.step();
    updateUI();
  });

  // Run button
  $('#btnRun').addEventListener('click', () => {
    if (runTimer) {
      clearInterval(runTimer);
      runTimer = null;
      statusInd.className = 'status-indicator';
      statusText.textContent = 'Paused';
      return;
    }

    const state = Simulator.getState();
    if (state.clock_cycle === 0) {
      if (!loadAndReset()) return;
    }

    if (state.program_done) {
      addUILog('Program already finished. Press Reset to restart.', 'info');
      return;
    }

    const speed = parseInt($('#speedSlider').value, 10);
    runTimer = setInterval(() => {
      const running = Simulator.step();
      updateUI();
      if (!running) {
        clearInterval(runTimer);
        runTimer = null;
      }
    }, speed);
  });

  // Reset button
  $('#btnReset').addEventListener('click', () => {
    if (runTimer) { clearInterval(runTimer); runTimer = null; }
    Simulator.reset();
    prevRegisters = new Array(32).fill(0);
    logConsole.innerHTML = '<div class="log-entry log-entry--info">Pipeline simulator reset. Ready.</div>';
    timelineBody.innerHTML = '';
    timelineHeader.innerHTML = '<th class="timeline-th timeline-th--instr">Instruction</th>';
    updateUI();
    renderMemoryFull();
  });

  // Load sample
  $('#btnLoadSample').addEventListener('click', () => {
    codeEditor.value = `ADDI R1 R0 5
ADDI R2 R0 10
ADD R1 R2 R3
SUB R1 R2 R4
MULI R5 R3 3
ANDI R6 R5 255
XORI R7 R6 127
SLL R8 R3 2
SRL R9 R3 1
SW R3 R0 1024
LW R10 R0 1024
BNE R1 R2 2
ADDI R11 R0 99
ADDI R12 R0 100
J 0`;
    updateLineNumbers();
    addUILog('Sample program loaded. Press Run or Step to begin.', 'info');
  });

  /* ── Initial State ─────────────────────────────────────────── */
  renderMemoryFull();

})();
