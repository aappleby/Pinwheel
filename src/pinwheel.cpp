#include "pinwheel.h"

//--------------------------------------------------------------------------------

void BlockRam::tick_read(logic<32> raddr) {
  out = data[b10(raddr, 2)];
}

void BlockRam::tick_write(logic<32> waddr, logic<32> wdata, logic<4> wmask, logic<1> wren) {
  if (wren) {
    logic<32> old_data = data[b10(waddr, 2)];
    logic<32> new_data = wdata;
    if (waddr[0]) new_data = new_data << 8;
    if (waddr[1]) new_data = new_data << 16;

    data[b10(waddr, 2)] = ((wmask[0] ? new_data : old_data) & 0x000000FF) |
                          ((wmask[1] ? new_data : old_data) & 0x0000FF00) |
                          ((wmask[2] ? new_data : old_data) & 0x00FF0000) |
                          ((wmask[3] ? new_data : old_data) & 0xFF000000);
  }
}

//--------------------------------------------------------------------------------

void Regfile::tick_read(logic<10> raddr1, logic<10> raddr2) {
  out_a = data[raddr1];
  out_b = data[raddr2];
}

void Regfile::tick_write(logic<10> waddr, logic<32> wdata, logic<1> wren) {
  if (wren) {
    data[waddr] = wdata;
  }
}

//--------------------------------------------------------------------------------

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

  pc = 0x00400000;

  debug_reg = 0;
}

//--------------------------------------------------------------------------------

logic<32> Pinwheel::tock_unpack(logic<3> f3, logic<32> addr, logic<32> data) {
  if (addr[0]) data = data >> 8;
  if (addr[1]) data = data >> 16;

  switch (f3) {
    case 0:  return sign_extend<32>( b8(data)); break;
    case 1:  return sign_extend<32>(b16(data)); break;
    case 2:  return data; break;
    case 3:  return data; break;
    case 4:  return zero_extend<32>( b8(data)); break;
    case 5:  return zero_extend<32>(b16(data)); break;
    case 6:  return data; break;
    case 7:  return data; break;
    default: return 0;
  }
}

//--------------------------------------------------------------------------------

logic<32> Pinwheel::tock_alu(logic<5> op, logic<3> f3, logic<7> f7, logic<32> imm, logic<32> pc, logic<32> reg_a, logic<32> reg_b) {
  switch(op) {
    case OP_JAL:     return pc + 4;
    case OP_JALR:    return pc + 4;
    case OP_LUI:     return imm;
    case OP_AUIPC:   return pc + imm;
    default:         break;
  }

  logic<32> alu_a = reg_a;
  logic<32> alu_b = op == OP_ALUI ? imm : reg_b;
  if (op == OP_ALU && f3 == 0 && f7 == 32) alu_b = -alu_b;

  switch (f3) {
    case 0:  return alu_a + alu_b; break;
    case 1:  return alu_a << b5(alu_b); break;
    case 2:  return signed(alu_a) < signed(alu_b); break;
    case 3:  return alu_a < alu_b; break;
    case 4:  return alu_a ^ alu_b; break;
    case 5:  return f7 == 32 ? signed(alu_a) >> b5(alu_b) : alu_a >> b5(alu_b); break;
    case 6:  return alu_a | alu_b; break;
    case 7:  return alu_a & alu_b; break;
    default: return 0;
  }
}

//--------------------------------------------------------------------------------
// graph match

logic<32> Pinwheel::tock_pc(logic<5> op, logic<3> f3, logic<32> imm, logic<32> pc, logic<32> reg_a, logic<32> reg_b) {
  logic<1> eq  = reg_a == reg_b;
  logic<1> slt = signed(reg_a) < signed(reg_b);
  logic<1> ult = reg_a < reg_b;

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

  switch (op) {
    case OP_BRANCH:  return take_branch ? pc + imm : pc + b32(4);
    case OP_JAL:     return pc + imm;
    case OP_JALR:    return reg_a + imm;
    default:         return pc + 4;
  }
}

//--------------------------------------------------------------------------------

logic<32> Pinwheel::tock_imm(logic<32> insn) {
  logic<5>  op    = b5(insn, 2);
  logic<32> imm_i = sign_extend<32>(b12(insn, 20));
  logic<32> imm_s = cat(dup<21>(insn[31]), b6(insn, 25), b5(insn, 7));
  logic<32> imm_u = b20(insn, 12) << 12;
  logic<32> imm_b = cat(dup<20>(insn[31]), insn[7], b6(insn, 25), b4(insn, 8), b1(0));
  logic<32> imm_j = cat(dup<12>(insn[31]), b8(insn, 12), insn[20], b10(insn, 21), b1(0));

  switch(op) {
    case OP_LOAD:   return imm_i;
    case OP_ALUI:   return imm_i;
    case OP_AUIPC:  return imm_u;
    case OP_STORE:  return imm_s;
    case OP_ALU:    return imm_i;
    case OP_LUI:    return imm_u;
    case OP_BRANCH: return imm_b;
    case OP_JALR:   return imm_i;
    case OP_JAL:    return imm_j;
    default:        return 0;
  }
}

//--------------------------------------------------------------------------------

MemPort Pinwheel::tock_bus(logic<5> op, logic<3> f3, logic<32> imm, logic<32> reg_a, logic<32> reg_b) {
  logic<32> addr  = reg_a + imm;
  logic<4>  mask  = 0;

  if (f3 == 0) mask = 0b0001;
  if (f3 == 1) mask = 0b0011;
  if (f3 == 2) mask = 0b1111;

  if (addr[0]) mask = mask << 1;
  if (addr[1]) mask = mask << 2;

  return {
    addr,
    reg_b,
    mask,
    op == OP_STORE
  };
}

//--------------------------------------------------------------------------------

RegPortWrite Pinwheel::tock_wb(logic<5> op, logic<5> rd, logic<32> rdata, logic<32> alu) {
  return {
    cat(b5(0), rd),
    op == OP_LOAD ? rdata : alu,
    rd != 0 && op != OP_STORE && op != OP_BRANCH
  };
}

//--------------------------------------------------------------------------------

logic<32> Pinwheel::tick_bus(MemPort port) {
  logic<1> data_cs    = b4(port.addr, 28) == 0x8;
  logic<1> debug_cs   = b4(port.addr, 28) == 0xF;

  data.tick_write(port.addr, port.wdata, port.wmask, port.wren && data_cs);
  data.tick_read (port.addr);
  debug_reg = port.wren && debug_cs ? port.wdata : debug_reg;

  if (data_cs)  return data.out;
  if (debug_cs) return debug_reg;
  return 0;
}

//--------------------------------------------------------------------------------

void Pinwheel::tick_singlecycle(logic<1> reset_in) {
  if (reset_in) {
    reset();
    return;
  }

  ticks++;

  code.tick_read(pc);

  logic<5>  op  = b5(code.out, 2);
  logic<5>  rd  = b5(code.out, 7);
  logic<3>  f3  = b3(code.out, 12);
  logic<5>  ra  = b5(code.out, 15);
  logic<5>  rb  = b5(code.out, 20);
  logic<7>  f7  = b7(code.out, 25);
  logic<32> imm = tock_imm(code.out);

  regfile.tick_read(b10(ra), b10(rb));

  auto bus_port  = tock_bus(op, f3, imm, regfile.out_a, regfile.out_b);
  auto bus_data  = tick_bus(bus_port);

  auto alu_out   = tock_alu(op, f3, f7, imm, pc, regfile.out_a, regfile.out_b);
  auto unpacked  = tock_unpack(f3, bus_port.addr, bus_data);
  auto writeback = tock_wb(op, rd, unpacked, alu_out);

  regfile.tick_write(writeback.addr, writeback.wdata, writeback.wren);

  pc = tock_pc(op, f3, imm, pc, regfile.out_a, regfile.out_b);
}

//--------------------------------------------------------------------------------
