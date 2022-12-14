#include "pinwheel_app.h"

#include "rvdisasm.h"

#define SDL_MAIN_HANDLED
#ifdef _MSC_VER
#include "SDL/include/SDL.h"
#include <windows.h>
#else
#include <SDL2/SDL.h>
#endif

#include "CoreLib/Dumper.h"
#include "CoreLib/Log.h"

//------------------------------------------------------------------------------

PinwheelApp::PinwheelApp() {
  pinwheel_sim = new PinwheelSim();
  sim_thread = new SimThread(pinwheel_sim);
}

//------------------------------------------------------------------------------

const char* PinwheelApp::app_get_title() {
  return "Pinwheel Test App";
}

//------------------------------------------------------------------------------

void PinwheelApp::app_init(int screen_w, int screen_h) {
  dvec2 screen_size(screen_w, screen_h);

  view_control.init(screen_size);
  text_painter.init();
  grid_painter.init(65536,65536);
  code_painter.init_hex_u32();
  //data_painter.init_hex_u8();
  data_painter.init_hex_u32();
  console_painter.init_ascii();
  box_painter.init();

  sim_thread->start();

  auto& pinwheel = pinwheel_sim->states.top();

  uint8_t* dontcare = (uint8_t*)(&pinwheel);
  for (int i = 0; i < sizeof(pinwheel); i++) {
    dontcare[i] = rand();
  }

  pinwheel.tock_twocycle(true);
  pinwheel.tick_twocycle(true);
}

//------------------------------------------------------------------------------

void PinwheelApp::app_close()  {
  LOG_G("PinwheelApp::app_close()\n");
  sim_thread->stop();
  delete sim_thread;
  delete pinwheel_sim;
  LOG_G("PinwheelApp::app_close() done\n");
}

//------------------------------------------------------------------------------

bool PinwheelApp::pause_when_idle() { return false; }

//------------------------------------------------------------------------------

void PinwheelApp::app_update(dvec2 screen_size, double delta)  {
  SDL_Event event;


  while (SDL_PollEvent(&event)) {


    if (event.type == SDL_KEYDOWN)
    switch (event.key.keysym.sym) {
      case SDLK_ESCAPE:
        view_control.view_target      = Viewport::screenspace(screen_size);
        view_control.view_target_snap = Viewport::screenspace(screen_size);
        break;
      case SDLK_RIGHT:  {
        sim_thread->pause();
        pinwheel_sim->states.push();

        int steps = 1;
        auto keyboard_state = SDL_GetKeyboardState(nullptr);
        if (keyboard_state[SDL_SCANCODE_LSHIFT])  steps *= 100;
        if (keyboard_state[SDL_SCANCODE_LALT])    steps *= 10;
        if (keyboard_state[SDL_SCANCODE_LCTRL])   steps *= 3;
        pinwheel_sim->steps += steps;

        sim_thread->resume();
        break;
      }
      case SDLK_LEFT: {
        sim_thread->pause();
        pinwheel_sim->states.pop();
        sim_thread->resume();
        break;
      }
      case SDLK_SPACE: {
        sim_thread->pause();
        pinwheel_sim->steps = 0;
        sim_thread->resume();
        break;
      }
    }

    if (event.type == SDL_MOUSEMOTION) {
      if (event.motion.state & SDL_BUTTON_LMASK) {
        view_control.pan(dvec2(event.motion.xrel, event.motion.yrel));
      }
    }

    if (event.type == SDL_MOUSEWHEEL) {
      int mouse_x = 0, mouse_y = 0;
      SDL_GetMouseState(&mouse_x, &mouse_y);
      view_control.on_mouse_wheel(dvec2(mouse_x, mouse_y), screen_size, double(event.wheel.y) * 0.25);
    }
  }
  view_control.update(delta);

  fflush(stdout);
}

//------------------------------------------------------------------------------

void PinwheelApp::app_render_frame(dvec2 screen_size, double delta)  {
  sim_thread->pause();

  auto& view = view_control.view_smooth_snap;
  grid_painter.render(view, screen_size);

#if 0
  StringDumper d;

  auto& pinwheel = pinwheel_sim->states.top();

  uint32_t insn_a = pinwheel.pc_a ? uint32_t(pinwheel.code.rdata()) : 0;

  d("hart_a        %d\n",     pinwheel.hart_a);
  d("pc_a          0x%08x\n", pinwheel.pc_a);
  d("insn_a        0x%08x ",  insn_a); print_rv(d, insn_a); d("\n");
  d("rs1 a         %d\n", b5(insn_a, 15));
  d("rs2 a         %d\n", b5(insn_a, 20));
  d("\n");

  d("hart_b        %d\n",     pinwheel.hart_b);
  d("pc_b          0x%08x\n", pinwheel.pc_b);
  d("insn_b        0x%08x ",  pinwheel.insn_b); print_rv(d, pinwheel.insn_b); d("\n");
  d("rs1 b         0x%08x\n", pinwheel.regs.get_rs1());
  d("rs2 b         0x%08x\n", pinwheel.regs.get_rs2());

  const auto imm_b  = pinwheel::decode_imm(pinwheel.insn_b);
  const auto addr_b = b32(pinwheel.regs.get_rs1() + imm_b);
  d("addr b        0x%08x\n", addr_b);

  d("\n");

  d("hart c        %d\n",     pinwheel.hart_c);
  d("pc c          0x%08x\n", pinwheel.pc_c);
  d("insn c        0x%08x ",  pinwheel.insn_c); print_rv(d, pinwheel.insn_c); d("\n");
  d("addr c        0x%08x\n", pinwheel.addr_c);
  d("result c      0x%08x\n", pinwheel.result_c);
  d("data c        0x%08x\n", pinwheel.data.rdata());
  d("\n");

  d("hart d        %d\n",     pinwheel.hart_d);
  d("pc d          0x%08x\n", pinwheel.pc_d);
  d("insn d        0x%08x ",  pinwheel.insn_d); print_rv(d, pinwheel.insn_d); d("\n");
  d("wb addr d     0x%08x\n", pinwheel.wb_addr_d);
  d("wb data d     0x%08x\n", pinwheel.wb_data_d);
  d("wb wren d     0x%08x\n", pinwheel.wb_wren_d);
  d("\n");

  d("debug_reg     0x%08x\n", pinwheel.debug_reg);
  d("ticks         %lld\n",   pinwheel.ticks);
  d("speed         %f\n",     double(sim_thread->sim_steps) / sim_thread->sim_time);
  d("states        %d\n",     pinwheel_sim->states.state_count());
  d("state bytes   %d\n",     pinwheel_sim->states.state_size_bytes());

  d("\n");

  for (int hart = 0; hart < 4; hart++) {
    auto r = &pinwheel.regs.get_data()[hart << 5];
    //d("hart %d vane %d pc 0x%08x", hart, hart_to_vane[hart], harts[hart]->pc);
    d("hart %d", hart);
    d("\n");
    d("r00 %08X  r08 %08X  r16 %08X  r24 %08X\n", r[ 0], r[ 8], r[16], r[24]);
    d("r01 %08X  r09 %08X  r17 %08X  r25 %08X\n", r[ 1], r[ 9], r[17], r[25]);
    d("r02 %08X  r10 %08X  r18 %08X  r26 %08X\n", r[ 2], r[10], r[18], r[26]);
    d("r03 %08X  r11 %08X  r19 %08X  r27 %08X\n", r[ 3], r[11], r[19], r[27]);
    d("r04 %08X  r12 %08X  r20 %08X  r28 %08X\n", r[ 4], r[12], r[20], r[28]);
    d("r05 %08X  r13 %08X  r21 %08X  r29 %08X\n", r[ 5], r[13], r[21], r[29]);
    d("r06 %08X  r14 %08X  r22 %08X  r30 %08X\n", r[ 6], r[14], r[22], r[30]);
    d("r07 %08X  r15 %08X  r23 %08X  r31 %08X\n", r[ 7], r[15], r[23], r[31]);
    d("\n");
  }

  text_painter.render_string(view, screen_size, d.s.c_str(), 32, 32);

  logic<32> hart0_pc = pinwheel.pc_a ? pinwheel.pc_a : pinwheel.pc_b;
  {
    d.clear();

    for (int i = -4; i <= 16; i++) {
      int offset = (hart0_pc & 0xFFFF) + (i * 4);
      int op = 0;

      if (offset < 0) op = 0;
      else if (offset > (65536 - 4)) op = 0;
      else op = pinwheel.code.get_data()[offset >> 2];

      d("%c0x%08x ", i == 0 ? '>' : ' ', hart0_pc + (i * 4));
      print_rv(d, op);
      d("\n");
    }

    text_painter.render_string(view, screen_size, d.s.c_str(), 320, 32);
  }

  code_painter.highlight_x = ((/*hart0_pc*/pinwheel.pc_b & 0xFFFF) >> 2) % 16;
  code_painter.highlight_y = ((/*hart0_pc*/pinwheel.pc_b & 0xFFFF) >> 2) / 16;
  code_painter.dump2(view, screen_size, 1024, 512, 0.5, 0.5, 64, 64, vec4(0.0, 0.0, 0.0, 0.4), (uint8_t*)pinwheel.code.get_data());

  data_painter.dump2(view, screen_size, 1024, 32, 1, 1, 64, 32, vec4(0.0, 0.0, 0.0, 0.4), (uint8_t*)pinwheel.data.get_data());

  console_painter.dump2(view, screen_size, 32*19,  32, 1, 1, Console::width, Console::height, vec4(0.0, 0.0, 0.0, 0.4), (uint8_t*)pinwheel.console1.buf);
  console_painter.dump2(view, screen_size, 32*19, 256, 1, 1, Console::width, Console::height, vec4(0.0, 0.0, 0.0, 0.4), (uint8_t*)pinwheel.console2.buf);
  console_painter.dump2(view, screen_size, 32*19, 480, 1, 1, Console::width, Console::height, vec4(0.0, 0.0, 0.0, 0.4), (uint8_t*)pinwheel.console3.buf);
  console_painter.dump2(view, screen_size, 32*19, 704, 1, 1, Console::width, Console::height, vec4(0.0, 0.0, 0.0, 0.4), (uint8_t*)pinwheel.console4.buf);
#endif

  //box_painter.push_corner_size(1024 + (harts[0]->pc % 64) * 14 - 1, 512 + (harts[0]->pc / 64) * 12, 12*4+2*3+2, 12, 0x8000FFFF);
  //box_painter.push_corner_size(1024 + (harts[1]->pc % 64) * 14 - 1, 512 + (harts[1]->pc / 64) * 12, 12*4+2*3+2, 12, 0x80FFFF00);
  //box_painter.push_corner_size(1024 + (harts[2]->pc % 64) * 14 - 1, 512 + (harts[2]->pc / 64) * 12, 12*4+2*3+2, 12, 0x80FF00FF);
  //box_painter.render(view, screen_size, 0, 0);

  sim_thread->resume();
}

//------------------------------------------------------------------------------

void PinwheelApp::app_render_ui(dvec2 screen_size, double delta) {

}

//------------------------------------------------------------------------------
