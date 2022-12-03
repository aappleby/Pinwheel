#pragma once
#include "metron_tools.h"
#include "constants.h"

#define wb =

static const int OP_LOAD    = 0b00000;
static const int OP_ALUI    = 0b00100;
static const int OP_AUIPC   = 0b00101;
static const int OP_STORE   = 0b01000;
static const int OP_ALU     = 0b01100;
static const int OP_LUI     = 0b01101;
static const int OP_BRANCH  = 0b11000;
static const int OP_JALR    = 0b11001;
static const int OP_JAL     = 0b11011;
static const int OP_SYS     = 0b11100;

inline const char* op_id_to_name(int op_id) {
  switch(op_id) {
    case OP_ALU:    return "OP_ALU   ";
    case OP_ALUI:   return "OP_ALUI  ";
    case OP_LOAD:   return "OP_LOAD  ";
    case OP_STORE:  return "OP_STORE ";
    case OP_BRANCH: return "OP_BRANCH";
    case OP_JAL:    return "OP_JAL   ";
    case OP_JALR:   return "OP_JALR  ";
    case OP_LUI:    return "OP_LUI   ";
    case OP_AUIPC:  return "OP_AUIPC ";
    default:        return "???      ";
  }
}

//------------------------------------------------------------------------------

struct cpu_to_mem {
  logic<32> addr;
  logic<32> data;
  logic<4>  mask;
};

struct cpu_to_regfile {
  logic<5>  raddr1;
  logic<5>  raddr2;
  logic<5>  waddr;
  logic<32> wdata;
};

struct registers_p0 {
  logic<5>  hart;
  logic<32> pc;
};

struct registers_p1 {
  logic<5>  hart;
  logic<32> pc;
  logic<32> insn;
};

struct registers_p2 {
  logic<5>  hart;
  logic<32> pc;
  logic<32> insn;

  logic<5>  align;
  logic<32> alu_out;
};

//------------------------------------------------------------------------------

class Pinwheel {
 public:
  void tock(logic<1> reset) {
    tick(reset);
  }

  uint32_t code_mem[16384];  // Cores share a 4K ROM
  uint32_t data_mem[16384];  // Cores share a 4K RAM
  uint32_t regfile[32][32]; // Cores have their own register file

  registers_p0 reg_p0;
  registers_p1 reg_p1;
  registers_p2 reg_p2;

  logic<32> ra;
  logic<32> rb;

  logic<32> dbus_data;
  logic<32> pbus_data;

  void dump() {
    if (reg_p0.hart == 0) {
      //printf("h%dp0: 0x%08x\n", (int)reg_p0.hart, (int)reg_p0.pc);
    }
    if (reg_p1.hart == 0) {
      //printf("h%dp1: 0x%08x %s inst 0x%08x imm 0x%08x\n", (int)reg_p1.hart, (int)reg_p1.pc, op_id_to_name(reg_p1.op), (uint32_t)reg_p1.insn, (uint32_t)reg_p1.imm);
    }
    if (reg_p2.hart == 0) {
      //printf("h%dp2: 0x%08x %s alu 0x%08x\n", (int)reg_p2.hart, (int)reg_p2.pc, op_id_to_name(reg_p2.op), (uint32_t)reg_p2.alu);
    }
  }

  //----------------------------------------

  void reset() {
    memset(code_mem, 0, sizeof(code_mem));
    memset(data_mem, 0, sizeof(data_mem));
    data_mem[0x3FC] = 0;

    std::string s;
    value_plusargs("text_file=%s", s);
    readmemh(s, code_mem);

    value_plusargs("data_file=%s", s);
    readmemh(s, data_mem);

    memset(regfile, 0, sizeof(regfile));

    reg_p0.hart   = 0;
    reg_p0.pc     = 0;

    reg_p1.hart   = 0;
    reg_p1.pc     = 0;
    reg_p1.insn   = 0;

    reg_p2.hart   = 1;
    reg_p2.pc     = -4;
    reg_p2.insn   = 0;

    ra = 0;
    rb = 0;
    dbus_data = 0;
    pbus_data = 0;
  }

  //--------------------------------------------------------------------------------

  static logic<32> unpack(logic<32> insn, logic<5>  align, logic<32> data) {
    logic<3>  f3 = b3(insn, 12);

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

  static logic<32> alu(logic<32> pc, logic<32> insn, logic<32> ra, logic<32> rb) {
    logic<1>  alu_alt = b1(insn, 30);
    logic<5>  op      = b5(insn, 2);
    logic<3>  f3      = b3(insn, 12);

    if (op == OP_ALU && f3 == 0 && alu_alt) rb = -rb;

    logic<32> imm_i = cat(dup<21>(insn[31]), b6(insn, 25), b5(insn, 20));
    logic<32> imm_u = cat(insn[31], b11(insn, 20), b8(insn, 12), b12(0));

    logic<3>  alu_op = f3;
    logic<32> a, b;
    switch(op) {
      case OP_ALU:     alu_op = f3;  a = ra;  b = rb;      break;
      case OP_ALUI:    alu_op = f3;  a = ra;  b = imm_i;   break;
      case OP_LOAD:    alu_op = f3;  a = ra;  b = rb;      break;
      case OP_STORE:   alu_op = f3;  a = ra;  b = rb;      break;
      case OP_BRANCH:  alu_op = f3;  a = ra;  b = rb;      break;
      case OP_JAL:     alu_op = 0;   a = pc;  b = b32(4);  break;
      case OP_JALR:    alu_op = 0;   a = pc;  b = b32(4);  break;
      case OP_LUI:     alu_op = 0;   a =  0;  b = imm_u;   break;
      case OP_AUIPC:   alu_op = 0;   a = pc;  b = imm_u;   break;
    }

    logic<32> alu_out;
    switch (alu_op) {
      case 0: alu_out = a + b;                 break;
      case 1: alu_out = a << b5(b);            break;
      case 2: alu_out = signed(a) < signed(b); break;
      case 3: alu_out = a < b;                 break;
      case 4: alu_out = a ^ b;                 break;
      case 5: alu_out = alu_alt ? signed(a) >> b5(b) : a >> b5(b); break;
      case 6: alu_out = a | b;                 break;
      case 7: alu_out = a & b;                 break;
    }
    return alu_out;
  }

  //--------------------------------------------------------------------------------

  cpu_to_mem dbus_out(logic<5> hart, logic<32> insn, logic<32> ra, logic<32> rb) {
    logic<5>  op    = b5(insn, 2);
    logic<3>  f3    = b3(insn, 12);

    logic<32> imm_i = cat(dup<21>(insn[31]), b6(insn, 25), b5(insn, 20));
    logic<32> imm_s = cat(dup<21>(insn[31]), b6(insn, 25), b5(insn, 7));

    logic<32> addr = ra + ((op == OP_STORE) ? imm_s : imm_i);
    logic<2>  align = b2(addr);
    logic<32> data = rb << (8 * align);

    logic<4> mask  = 0b0000;
    if (op == OP_STORE) {
      if      (f3 == 0) mask = 0b0001 << align;
      else if (f3 == 1) mask = 0b0011 << align;
      else if (f3 == 2) mask = 0b1111;
      else              mask = 0b0000;
    }

    if (!hart) {
      addr  = 0;
      data = 0;
      mask  = 0;
    }

    return { addr, data, mask };
  }

  //--------------------------------------------------------------------------------

  cpu_to_mem rbus_out() {
    return {0,0,0};
  }

  //--------------------------------------------------------------------------------

  void tick_dbus(logic<32> addr, logic<32> data, logic<4> mask) {
    logic<32> new_data = data_mem[b10(addr, 2)];
    if (mask) {
      if (addr != 0x40000000) {
        if (mask[0]) new_data = (new_data & 0xFFFFFF00) | (data & 0x000000FF);
        if (mask[1]) new_data = (new_data & 0xFFFF00FF) | (data & 0x0000FF00);
        if (mask[2]) new_data = (new_data & 0xFF00FFFF) | (data & 0x00FF0000);
        if (mask[3]) new_data = (new_data & 0x00FFFFFF) | (data & 0xFF000000);
        data_mem[b10(addr, 2)] wb new_data;
      }
    }
    dbus_data wb new_data;
  }

  //--------------------------------------------------------------------------------

  void tick_pbus(logic<32> addr) {
    pbus_data wb code_mem[b10(addr, 2)];
  }

  //--------------------------------------------------------------------------------

  void tick_rbus_read(logic<5> read_hart, logic<32> read_insn) {
    logic<5> rbus_raddr1 = b5(read_insn, 15);
    logic<5> rbus_raddr2 = b5(read_insn, 20);

    ra wb regfile[read_hart][rbus_raddr1];
    rb wb regfile[read_hart][rbus_raddr2];
  }

  void tick_rbus_write(logic<5> write_hart, logic<32> write_insn, logic<5> align, logic<32> data, logic<32> alu_out) {
    logic<5>  op = b5(write_insn, 2);
    logic<32> unpacked = unpack(write_insn, align, data);
    logic<32> rbus_wdata = op == OP_LOAD ? unpacked : alu_out;

    logic<5>  rbus_waddr = b5(write_insn, 7);
    if (op == OP_STORE)  rbus_waddr = 0;
    if (op == OP_BRANCH) rbus_waddr = 0;

    if (rbus_waddr) {
      regfile[write_hart][rbus_waddr] wb rbus_wdata;
    }
  }

  void tick_rbus(cpu_to_regfile bus) {
  }

  //--------------------------------------------------------------------------------

  logic<32> next_pc(logic<5>  hart, logic<32> pc, logic<32> insn) {
    logic<5>  op    = b5(insn, 2);
    logic<3>  f3    = b3(insn, 12);
    logic<32> imm_i = cat(dup<21>(insn[31]), b6(insn, 25), b5(insn, 20));
    logic<32> imm_b = cat(dup<20>(insn[31]), insn[7], b6(insn, 25), b4(insn, 8), b1(0));
    logic<32> imm_j = cat(dup<12>(insn[31]), b8(insn, 12), insn[20], b6(insn, 25), b4(insn, 21), b1(0));

    logic<1> eq  = ra == rb;
    logic<1> slt = signed(ra) < signed(rb);
    logic<1> ult = ra < rb;

    logic<1> jump_rel = 0;
    if (op == OP_BRANCH) {
      switch (f3) {
        case 0: jump_rel =   eq; break;
        case 1: jump_rel =  !eq; break;
        case 2: jump_rel =   eq; break;
        case 3: jump_rel =  !eq; break;
        case 4: jump_rel =  slt; break;
        case 5: jump_rel = !slt; break;
        case 6: jump_rel =  ult; break;
        case 7: jump_rel = !ult; break;
      }
    }

    logic<32> next_pc;
    if      (op == OP_BRANCH && jump_rel) { next_pc = pc + imm_b; }
    else if (op == OP_JAL)                { next_pc = pc + imm_j; }
    else if (op == OP_JALR)               { next_pc = ra + imm_i; }
    else                                  { next_pc = pc + b32(4); }
    return next_pc;
  }

  //--------------------------------------------------------------------------------

  void tick(logic<1> reset_in) {
    if (reset_in) {
      reset();
      return;
    }

    const auto old_reg_p0 = reg_p0;
    const auto old_reg_p1 = reg_p1;
    const auto old_reg_p2 = reg_p2;
    const auto old_ra = ra;
    const auto old_rb = rb;
    const auto old_dbus_data = dbus_data;
    const auto old_pbus_data = pbus_data;

    registers_p0 new_reg_p0;
    registers_p1 new_reg_p1;
    registers_p2 new_reg_p2;

    //----------------------------------------

    new_reg_p1.hart wb old_reg_p0.hart;
    new_reg_p1.pc   wb old_reg_p0.pc;
    new_reg_p1.insn wb old_pbus_data;

    //----------------------------------------

    auto to_dbus = dbus_out(old_reg_p1.hart, old_reg_p1.insn, old_ra, old_rb);
    tick_dbus(to_dbus.addr, to_dbus.data, to_dbus.mask);

    //----------------------------------------

    new_reg_p2.hart    wb old_reg_p1.hart;
    new_reg_p2.pc      wb next_pc(old_reg_p1.hart, old_reg_p1.pc, old_reg_p1.insn);
    new_reg_p2.insn    wb old_reg_p1.insn;
    new_reg_p2.align   wb b2(to_dbus.addr);
    new_reg_p2.alu_out wb alu(old_reg_p1.pc, old_reg_p1.insn, old_ra, old_rb);

    //----------------------------------------

    new_reg_p0 wb {
      .hart = old_reg_p2.hart,
      .pc   = old_reg_p2.pc,
    };

    //----------------------------------------

    logic<32> unpacked = unpack(old_reg_p2.insn, old_reg_p2.align, old_dbus_data);
    logic<5>  write_op = b5(old_reg_p2.insn, 2);
    logic<32> rbus_wdata = write_op == OP_LOAD ? unpacked : old_reg_p2.alu_out;

    tick_rbus_write(old_reg_p2.hart, old_reg_p2.insn, old_reg_p2.align, old_dbus_data, old_reg_p2.alu_out);
    tick_rbus_read (old_reg_p0.hart, old_pbus_data);

    tick_pbus(old_reg_p2.pc);

    reg_p0 = new_reg_p0;
    reg_p1 = new_reg_p1;
    reg_p2 = new_reg_p2;

  }
};
