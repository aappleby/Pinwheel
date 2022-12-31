#include "pinwheel.h"

//--------------------------------------------------------------------------------

void BlockRam::tick_read(logic<32> raddr, logic<1> rden) {
  if (rden) {
    out = data[b10(raddr, 2)];
  }
  else {
    out = 0;
  }
}

void BlockRam::tick_write(logic<32> waddr, logic<32> wdata, logic<4> wmask, logic<1> wren) {
  if (wren) {
    logic<32> old_data = data[b10(waddr, 2)];
    logic<32> new_data = wdata << (8 * b2(waddr));
    if (!wmask[0]) new_data = (new_data & 0xFFFFFF00) | (old_data & 0x000000FF);
    if (!wmask[1]) new_data = (new_data & 0xFFFF00FF) | (old_data & 0x0000FF00);
    if (!wmask[2]) new_data = (new_data & 0xFF00FFFF) | (old_data & 0x00FF0000);
    if (!wmask[3]) new_data = (new_data & 0x00FFFFFF) | (old_data & 0xFF000000);
    data[b10(waddr, 2)] = new_data;
  }
}

//--------------------------------------------------------------------------------

void BlockRegfile::tick_read(logic<10> raddr1, logic<10> raddr2, logic<1> rden) {
  if (rden) {
    out_a = data[raddr1];
    out_b = data[raddr2];
  }
  else {
    out_a = 0;
    out_b = 0;
  }
}

void BlockRegfile::tick_write(logic<10> waddr, logic<32> wdata, logic<1> wren) {
  if (wren) {
    data[waddr] = wdata;
  }
}

//--------------------------------------------------------------------------------

Pinwheel::Pinwheel() {
}

Pinwheel* Pinwheel::clone() {
  Pinwheel* p = new Pinwheel();
  memcpy(p, this, sizeof(*this));
  return p;
}

//--------------------------------------------------------------------------------

void Pinwheel::reset() {
  memset(this, 0, sizeof(*this));

  std::string s;
  value_plusargs("text_file=%s", s);
  readmemh(s, code.data);

  value_plusargs("data_file=%s", s);
  readmemh(s, data.data);

  vane0.pc = 0x00400000;
  vane1.pc = 0x00400000;
  vane2.pc = 0x00400000;

  vane0.hart = 0;
  vane1.hart = 1;
  vane2.hart = 2;

  vane0.enable = 1;

  debug_reg = 0;
}

//--------------------------------------------------------------------------------

logic<32> Pinwheel::unpack(logic<32> insn, logic<32> addr, logic<32> data) {
  logic<2> align = b2(addr);
  logic<3> f3 = b3(insn, 12);

  switch (f3) {
    case 0:  return sign_extend<32>( b8(data, align << 3)); break;
    case 1:  return sign_extend<32>(b16(data, align << 3)); break;
    case 2:  return data; break;
    case 3:  return data; break;
    case 4:  return zero_extend<32>( b8(data, align << 3)); break;
    case 5:  return zero_extend<32>(b16(data, align << 3)); break;
    case 6:  return data; break;
    case 7:  return data; break;
    default: return 0;
  }
}

//--------------------------------------------------------------------------------

logic<32> Pinwheel::alu(logic<32> insn, logic<32> pc, logic<32> reg_a, logic<32> reg_b) {
  logic<5> op  = b5(insn, 2);
  logic<3> f3  = b3(insn, 12);
  logic<1> alt = b1(insn, 30);

  if (op == OP_ALU && f3 == 0 && alt) reg_b = -reg_b;

  logic<32> imm_i = sign_extend<32>(b12(insn, 20));
  logic<32> imm_u = b20(insn, 12) << 12;

  logic<3>  alu_op;
  logic<32> a, b;

  switch(op) {
    case OP_ALU:     alu_op = f3;  a = reg_a; b = reg_b;   break;
    case OP_ALUI:    alu_op = f3;  a = reg_a; b = imm_i;   break;
    case OP_JAL:     return pc + 4;
    case OP_JALR:    return pc + 4;
    case OP_LUI:     return imm_u;
    case OP_AUIPC:   return pc + imm_u;
    default:         return 0;
  }

  switch (alu_op) {
    case 0:  return a + b; break;
    case 1:  return a << b5(b); break;
    case 2:  return signed(a) < signed(b); break;
    case 3:  return a < b; break;
    case 4:  return a ^ b; break;
    case 5:  return alt ? signed(a) >> b5(b) : a >> b5(b); break;
    case 6:  return a | b; break;
    case 7:  return a & b; break;
    default: return 0;
  }
}

//--------------------------------------------------------------------------------

logic<32> Pinwheel::pc_gen(logic<32> pc, logic<32> insn, logic<1> active, logic<32> reg_a, logic<32> reg_b) {
  if (!active) {
    return pc;
  }

  logic<5> op  = b5(insn, 2);
  logic<1> eq  = reg_a == reg_b;
  logic<1> slt = signed(reg_a) < signed(reg_b);
  logic<1> ult = reg_a < reg_b;

  logic<3> f3 = b3(insn, 12);
  logic<1> take_branch;
  switch (f3) {
    case 0:  take_branch =   eq; break;
    case 1:  take_branch =  !eq; break;
    case 2:  take_branch =   eq; break;
    case 3:  take_branch =  !eq; break;
    case 4:  take_branch =  slt; break;
    case 5:  take_branch = !slt; break;
    case 6:  take_branch =  ult; break;
    case 7:  take_branch = !ult; break;
    default: take_branch =    0; break;
  }

  if (op == OP_BRANCH) {
    logic<32> imm_b = cat(dup<20>(insn[31]), insn[7], b6(insn, 25), b4(insn, 8), b1(0));
    return pc + (take_branch ? imm_b : b32(4));
  }
  else if (op == OP_JAL) {
    logic<32> imm_j = cat(dup<12>(insn[31]), b8(insn, 12), insn[20], b10(insn, 21), b1(0));
    return pc + imm_j;
  }
  else if (op == OP_JALR) {
    logic<32> imm_i = sign_extend<32>(b12(insn, 20));
    return reg_a + imm_i;
  }
  else {
    return pc + 4;
  }
}

//--------------------------------------------------------------------------------

logic<32> Pinwheel::addr_gen(logic<32> insn, logic<32> reg_a) {
  logic<5>  op    = b5(insn, 2);
  logic<32> imm_i = sign_extend<32>(b12(insn, 20));
  logic<32> imm_s = cat(dup<21>(insn[31]), b6(insn, 25), b5(insn, 7));
  logic<32> addr  = reg_a + ((op == OP_STORE) ? imm_s : imm_i);
  return addr;
}

//--------------------------------------------------------------------------------

logic<4> Pinwheel::mask_gen(logic<32> insn, logic<32> addr) {
  logic<5> op = b5(insn, 2);
  if (op != OP_STORE) return 0;

  logic<3> f3 = b3(insn, 12);
  logic<2> align = b2(addr);

  if      (f3 == 0) return 0b0001 << align;
  else if (f3 == 1) return 0b0011 << align;
  else if (f3 == 2) return 0b1111;
  else              return 0;
}

//--------------------------------------------------------------------------------

void Pinwheel::tick(logic<1> reset_in) {
  if (reset_in) {
    reset();
    return;
  }

  ticks++;

  //----------

  const auto vane0 = this->vane0;
  const auto vane1 = this->vane1;
  const auto vane2 = this->vane2;

  this->vane0 = vane2;
  this->vane1 = vane0;
  this->vane2 = vane1;

  //----------

  logic<5>  vane0_op = b5(vane0.insn, 2);
  logic<5>  vane1_op = b5(vane1.insn, 2);
  logic<5>  vane2_op = b5(vane2.insn, 2);

  // Mask out r0 if we read it from the regfile.
  logic<32> vane1_reg_a = b5(vane1.insn, 15) ? regs.out_a : b32(0);
  logic<32> vane1_reg_b = b5(vane1.insn, 20) ? regs.out_b : b32(0);

  logic<32> code_rdata = code.out;
  logic<32> data_rdata = data.out;

  //----------
  // Use the saved read address to select between possible data sources.

  logic<32> bus_rdata;
  switch(b4(vane2_mem_addr, 28)) {
    case 0x0: bus_rdata = code_rdata; break;
    case 0x1: bus_rdata = regs.out_a; break; // note we do _not_ mask out r0
    case 0x8: bus_rdata = data_rdata; break;
    case 0xF: bus_rdata = debug_reg;  break;
    default:  bus_rdata = DONTCARE;   break;
  }

  bus_rdata = unpack(vane2.insn, vane2_mem_addr, bus_rdata);

  //----------

  logic<32> vane1_mem_addr  = addr_gen(vane1.insn, vane1_reg_a);
  logic<1>  vane1_mem_rden  = vane1.active && vane1_op == OP_LOAD;
  logic<1>  vane1_mem_wren  = vane1.active && vane1_op == OP_STORE;
  logic<32> vane1_mem_wdata = vane1_reg_b;
  logic<4>  vane1_mem_wmask = mask_gen(vane1.insn, vane1_mem_addr);

  logic<1> code_cs    = b4(vane1_mem_addr, 28) == 0x0;
  logic<1> data_cs    = b4(vane1_mem_addr, 28) == 0x8;
  logic<1> debug_cs   = b4(vane1_mem_addr, 28) == 0xF;
  logic<1> regfile_cs = b4(vane1_mem_addr, 28) == 0x1;

  this->data.tick_read (vane1_mem_addr, vane1_mem_rden && data_cs);
  this->data.tick_write(vane1_mem_addr, vane1_mem_wdata, vane1_mem_wmask, vane1_mem_wren && data_cs);

  if (vane1_mem_wren && debug_cs) this->debug_reg = vane1_mem_wdata;

  //----------

  {
    logic<10> vane0_reg_raddr1 = cat(vane0.hart, b5(code_rdata, 15));
    logic<10> vane0_reg_raddr2 = cat(vane0.hart, b5(code_rdata, 20));
    logic<1>  vane0_reg_rden   = vane0.active;

    logic<10> vane1_reg_raddr1 = b10(vane1_mem_addr, 2);
    logic<10> vane1_reg_raddr2 = DONTCARE;
    logic<1>  vane1_reg_rden   = vane1_mem_rden && regfile_cs;

    if (vane0_reg_rden) {
      this->regs.tick_read(vane0_reg_raddr1, vane0_reg_raddr2, vane0_reg_rden);
    }
    else if (vane1_reg_rden) {
      this->regs.tick_read(vane1_reg_raddr1, vane1_reg_raddr2, vane1_reg_rden);
    }
    else {
      this->regs.tick_read(DONTCARE, DONTCARE, false);
    }
  }

  //----------

  {
    logic<10> vane2_reg_waddr = cat(vane2.hart, b5(vane2.insn, 7));
    logic<1>  vane2_reg_wren  = (vane2.enable | vane2.active) && vane2_reg_waddr != 0 && vane2_op != OP_STORE && vane2_op != OP_BRANCH;
    logic<32> vane2_reg_wdata = vane2_op == OP_LOAD ? bus_rdata : vane2_alu_out;

    logic<10> vane1_reg_waddr = b10(vane1_mem_addr, 2);
    logic<1>  vane1_reg_wren  = vane1_mem_wren && regfile_cs;
    logic<32> vane1_reg_wdata = vane1_reg_b;

    if (vane2_reg_wren) {
      this->regs.tick_write(vane2_reg_waddr, vane2_reg_wdata, vane2_reg_wren);
    }
    else if (vane1_reg_wren) {
      this->regs.tick_write(vane1_reg_waddr, vane1_reg_wdata, vane1_reg_wren);
    }
    else {
      this->regs.tick_write(DONTCARE, DONTCARE, false);
    }
  }

  //----------
  // Code bus mux

  if (vane2.enable | vane2.active) {
    this->code.tick_read(vane2.pc, true);
    this->code.tick_write(0, 0, 0, false);
  }
  else {
    this->code.tick_read(vane1_mem_addr, vane1_mem_rden && code_cs);
    this->code.tick_write(0, 0, 0, false);
  }

  //----------

  // Vane 0 becomes active if vane 2 was set to enable
  this->vane0.active = vane2.enable | vane2.active;

  // Vane 1 picks up the instruction from the code bus
  this->vane1.insn = vane0.active ? code_rdata : b32(0);

  // Vane 2 updates the PC from vane 1
  this->vane2.pc = this->pc_gen(vane1.pc, vane1.insn, vane1.active, vane1_reg_a, vane1_reg_b);

  // Vane 2 stores a copy of the alu output from vane 1
  this->vane2_alu_out = this->alu(vane1.insn, vane1.pc, vane1_reg_a, vane1_reg_b);

  // Vane 2 stores a copy of the memory address from vane 1
  this->vane2_mem_addr = vane1_mem_addr;
}

//--------------------------------------------------------------------------------
