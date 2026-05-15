/*
 * PicoDrive
 * (C) notaz, 2007-2010
 *
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */

#include <stddef.h>
#include "pico_int.h"
#include "memory.h"
#include "../../e9k-lib/e9k-lib.h"
#include "../../e9k-lib/e9k-z80-dasm.h"

uptr z80_read_map [0x10000 >> Z80_MEM_SHIFT];
uptr z80_write_map[0x10000 >> Z80_MEM_SHIFT];
static u8 mega_z80_debug_breakpoints[0x10000 / 8];
static size_t mega_z80_debug_breakpoint_count;
static u16 mega_z80_debug_suppressed_breakpoint_addr;
static int mega_z80_debug_suppressed_breakpoint_active;
static int mega_z80_debug_step_pending;

extern void e9k_debug_pause(void);
extern void e9k_debug_resume(void);

u32 z80_read(u32 a)
{
  uptr v;
  a &= 0x00ffff;
  v = z80_read_map[a >> Z80_MEM_SHIFT];
  if (map_flag_set(v))
    return ((z80_read_f *)(map_to_function(v)))(a);
  else
    return *(u8 *)((v << 1) + a);
}


#ifdef _USE_DRZ80
// this causes trouble in some cases, like doukutsu putting sp in bank area
// no perf difference for most, upto 1-2% for some others
//#define FAST_Z80SP

struct DrZ80 drZ80;
// import flag conversion from DrZ80
extern u8 DrZ80_ARM[];
extern u8 DrARM_Z80[];

static void drz80_load_pcsp(u32 pc, u32 sp)
{
  drZ80.Z80PC_BASE = z80_read_map[pc >> Z80_MEM_SHIFT];
  if (drZ80.Z80PC_BASE & (1<<31)) {
    elprintf(EL_STATUS|EL_ANOMALY, "load_pcsp: bad PC: %04x", pc);
    drZ80.Z80PC_BASE = drZ80.Z80PC = z80_read_map[0];
  } else {
    drZ80.Z80PC_BASE <<= 1;
    drZ80.Z80PC = drZ80.Z80PC_BASE + pc;
  }
  drZ80.Z80SP = sp;
#ifdef FAST_Z80SP
  drZ80.Z80SP_BASE = z80_read_map[sp >> Z80_MEM_SHIFT];
  if (drZ80.Z80SP_BASE & (1<<31)) {
    elprintf(EL_STATUS|EL_ANOMALY, "load_pcsp: bad SP: %04x", sp);
    drZ80.Z80SP_BASE = z80_read_map[0];
    drZ80.Z80SP = drZ80.Z80SP_BASE + (1 << Z80_MEM_SHIFT);
  } else {
    drZ80.Z80SP_BASE <<= 1;
    drZ80.Z80SP = drZ80.Z80SP_BASE + sp;
  }
#endif
}

// called only if internal xmap rebase fails
static unsigned int dz80_rebase_pc(unsigned short pc)
{
  elprintf(EL_STATUS|EL_ANOMALY, "dz80_rebase_pc: fail on %04x", pc);
  drZ80.Z80PC_BASE = z80_read_map[0] << 1;
  return drZ80.Z80PC_BASE;
}

static void dz80_noop_irq_ack(void) {}

#ifdef FAST_Z80SP
static u32 drz80_sp_base;

static unsigned int dz80_rebase_sp(unsigned short sp)
{
  elprintf(EL_STATUS|EL_ANOMALY, "dz80_rebase_sp: fail on %04x", sp);
  drZ80.Z80SP_BASE = z80_read_map[drz80_sp_base >> Z80_MEM_SHIFT] << 1;
  return drZ80.Z80SP_BASE + (1 << Z80_MEM_SHIFT) - 0x100;
}
#else
#define dz80_rebase_sp NULL
#endif
#endif // _USE_DRZ80


void z80_init(void)
{
#ifdef _USE_DRZ80
  memset(&drZ80, 0, sizeof(drZ80));
  drZ80.z80_rebasePC = dz80_rebase_pc;
  drZ80.z80_rebaseSP = dz80_rebase_sp;
  drZ80.z80_read8    = (void *)z80_read_map;
  drZ80.z80_read16   = NULL;
  drZ80.z80_write8   = (void *)z80_write_map;
  drZ80.z80_write16  = NULL;
  drZ80.z80_irq_callback = NULL;
#endif
#ifdef _USE_CZ80
  memset(&CZ80, 0, sizeof(CZ80));
  Cz80_Init(&CZ80);
  Cz80_Set_ReadB(&CZ80, NULL); // unused (hacked in)
  Cz80_Set_WriteB(&CZ80, NULL);
#endif
}

void z80_reset(void)
{
  int is_sms = (PicoIn.AHW & (PAHW_SMS|PAHW_SG|PAHW_SC)) == PAHW_SMS;
  mega_z80_debug_suppressed_breakpoint_active = 0;
  mega_z80_debug_step_pending = 0;
#ifdef _USE_DRZ80
  drZ80.Z80I = 0;
  drZ80.Z80IM = 0;
  drZ80.Z80IF = 0;
  drZ80.z80irqvector = 0xff0000; // RST 38h
  drZ80.Z80PC_BASE = drZ80.Z80PC = z80_read_map[0] << 1;
  // other registers not changed, undefined on cold boot
#ifdef FAST_Z80SP
  // drZ80 is locked in single bank
  drz80_sp_base = (PicoIn.AHW & PAHW_8BIT) ? 0xc000 : 0x0000;
  drZ80.Z80SP_BASE = z80_read_map[drz80_sp_base >> Z80_MEM_SHIFT] << 1;
#endif
  drZ80.Z80SP = drZ80.Z80SP_BASE + (is_sms ? 0xdff0 : 0xffff); // simulate BIOS
  drZ80.z80_irq_callback = NULL; // use auto-clear
  if (PicoIn.AHW & PAHW_8BIT)
    drZ80.z80_irq_callback = dz80_noop_irq_ack;
  // XXX: since we use direct SP pointer, it might make sense to force it to RAM,
  // but we'll rely on built-in stack protection for now
#endif
#ifdef _USE_CZ80
  Cz80_Reset(&CZ80);
  Cz80_Set_Reg(&CZ80, CZ80_SP, 0xffff);
  if (is_sms)
    Cz80_Set_Reg(&CZ80, CZ80_SP, 0xdff0);
#endif
}

struct z80sr_main {
  u8 a, f;
  u8 b, c;
  u8 d, e;
  u8 h, l;
};

struct z80_state {
  char magic[4];
  // regs
  struct z80sr_main m; // main regs
  struct z80sr_main a; // alt (') regs
  u8  i, r;
  u16 ix, iy;
  u16 sp;
  u16 pc;
  // other
  u8 halted;
  u8 iff1, iff2;
  u8 im;            // irq mode
  u8 irq_pending;   // irq line level, 1 if active
  u8 irq_vector[3]; // up to 3 byte vector for irq mode0 handling
  u16 cyc;
  u16 busdelay;
  u8 reserved[4];
};

void z80_pack(void *data)
{
  struct z80_state *s = data;
  memset(data, 0, Z80_STATE_SIZE);
  memcpy(s->magic, "Z80a", 4);
  s->cyc = Pico.t.z80c_cnt;
  s->busdelay = Pico.t.z80_busdelay;
#if defined(_USE_DRZ80)
  #define DRR8(n)   (drZ80.Z80##n >> 24)
  #define DRR16(n)  (drZ80.Z80##n >> 16)
  #define DRR16H(n) (drZ80.Z80##n >> 24)
  #define DRR16L(n) ((drZ80.Z80##n >> 16) & 0xff)
  s->m.a = DRR8(A);     s->m.f = DrARM_Z80[drZ80.Z80F];
  s->m.b = DRR16H(BC);  s->m.c = DRR16L(BC);
  s->m.d = DRR16H(DE);  s->m.e = DRR16L(DE);
  s->m.h = DRR16H(HL);  s->m.l = DRR16L(HL);
  s->a.a = DRR8(A2);    s->a.f = DrARM_Z80[drZ80.Z80F2];
  s->a.b = DRR16H(BC2); s->a.c = DRR16L(BC2);
  s->a.d = DRR16H(DE2); s->a.e = DRR16L(DE2);
  s->a.h = DRR16H(HL2); s->a.l = DRR16L(HL2);
  s->i = DRR8(I);       s->r = drZ80.spare;
  s->ix = DRR16(IX);    s->iy = DRR16(IY);
  s->sp = drZ80.Z80SP - drZ80.Z80SP_BASE;
  s->pc = drZ80.Z80PC - drZ80.Z80PC_BASE;
  s->halted = !!(drZ80.Z80IF & 4);
  s->iff1 = !!(drZ80.Z80IF & 1);
  s->iff2 = !!(drZ80.Z80IF & 2);
  s->im = drZ80.Z80IM;
  s->irq_pending = !!drZ80.Z80_IRQ;
  s->irq_vector[0] = drZ80.z80irqvector >> 16;
  s->irq_vector[1] = drZ80.z80irqvector >> 8;
  s->irq_vector[2] = drZ80.z80irqvector;
#elif defined(_USE_CZ80)
  {
    const cz80_struc *CPU = &CZ80;
    s->m.a = zA;  s->m.f = zF;
    s->m.b = zB;  s->m.c = zC;
    s->m.d = zD;  s->m.e = zE;
    s->m.h = zH;  s->m.l = zL;
    s->a.a = zA2; s->a.f = zF2;
    s->a.b = CZ80.BC2.B.H; s->a.c = CZ80.BC2.B.L;
    s->a.d = CZ80.DE2.B.H; s->a.e = CZ80.DE2.B.L;
    s->a.h = CZ80.HL2.B.H; s->a.l = CZ80.HL2.B.L;
    s->i  = zI;   s->r  = (zR & 0x7f) | zR2;
    s->ix = zIX;  s->iy = zIY;
    s->sp = Cz80_Get_Reg(&CZ80, CZ80_SP);
    s->pc = Cz80_Get_Reg(&CZ80, CZ80_PC);
    s->halted = !!Cz80_Get_Reg(&CZ80, CZ80_HALT);
    s->iff1 = !!zIFF1;
    s->iff2 = !!zIFF2;
    s->im = zIM;
    s->irq_pending = (Cz80_Get_Reg(&CZ80, CZ80_IRQ) != CLEAR_LINE);
    s->irq_vector[0] = 0xff;
  }
#endif
}

int z80_unpack(const void *data)
{
  const struct z80_state *s = data;
  if (memcmp(s->magic, "Z80", 3) != 0) {
    elprintf(EL_STATUS, "legacy z80 state - ignored");
    return 0;
  }
  Pico.t.z80c_cnt = s->cyc;
  Pico.t.z80_busdelay = s->busdelay;

#if defined(_USE_DRZ80)
  #define DRW8(n, v)       drZ80.Z80##n = (u32)(v) << 24
  #define DRW16(n, v)      drZ80.Z80##n = (u32)(v) << 16
  #define DRW16HL(n, h, l) drZ80.Z80##n = ((u32)(h) << 24) | ((u32)(l) << 16)
  u8 mf, af;
  if (s->magic[3] == 'a') {
    // new save: flags always in Z80 format
    mf = DrZ80_ARM[s->m.f];
    af = DrZ80_ARM[s->a.f];
  } else {
    // NB hack, swap Flag3 and NFlag for save file compatibility
    mf = (s->m.f & 0x9f)|((s->m.f & 0x40)>>1)|((s->m.f & 0x20)<<1);
    af = (s->a.f & 0x9f)|((s->a.f & 0x40)>>1)|((s->a.f & 0x20)<<1);
  }
  DRW8(A, s->m.a);  drZ80.Z80F = mf;
  DRW16HL(BC, s->m.b, s->m.c);
  DRW16HL(DE, s->m.d, s->m.e);
  DRW16HL(HL, s->m.h, s->m.l);
  DRW8(A2, s->a.a); drZ80.Z80F2 = af;
  DRW16HL(BC2, s->a.b, s->a.c);
  DRW16HL(DE2, s->a.d, s->a.e);
  DRW16HL(HL2, s->a.h, s->a.l);
  DRW8(I, s->i);    drZ80.spare = s->r;
  DRW16(IX, s->ix); DRW16(IY, s->iy);
  drz80_load_pcsp(s->pc, s->sp);
  drZ80.Z80IF = 0;
  if (s->halted) drZ80.Z80IF |= 4;
  if (s->iff1)   drZ80.Z80IF |= 1;
  if (s->iff2)   drZ80.Z80IF |= 2;
  drZ80.Z80IM = s->im;
  drZ80.Z80_IRQ = s->irq_pending;
  drZ80.z80irqvector = ((u32)s->irq_vector[0] << 16) |
    ((u32)s->irq_vector[1] << 8) | s->irq_vector[2];
  return 0;
#elif defined(_USE_CZ80)
  {
    cz80_struc *CPU = &CZ80;
    zA  = s->m.a; zF  = s->m.f;
    zB  = s->m.b; zC  = s->m.c;
    zD  = s->m.d; zE  = s->m.e;
    zH  = s->m.h; zL  = s->m.l;
    zA2 = s->a.a; zF2 = s->a.f;
    CZ80.BC2.B.H = s->a.b; CZ80.BC2.B.L = s->a.c;
    CZ80.DE2.B.H = s->a.d; CZ80.DE2.B.L = s->a.e;
    CZ80.HL2.B.H = s->a.h; CZ80.HL2.B.L = s->a.l;
    zI  = s->i;   zR  = s->r; zR2 = s->r & 0x80;
    zIX = s->ix;  zIY = s->iy;
    Cz80_Set_Reg(&CZ80, CZ80_SP, s->sp);
    Cz80_Set_Reg(&CZ80, CZ80_PC, s->pc);
    Cz80_Set_Reg(&CZ80, CZ80_HALT, s->halted);
    Cz80_Set_Reg(&CZ80, CZ80_IFF1, s->iff1);
    Cz80_Set_Reg(&CZ80, CZ80_IFF2, s->iff2);
    zIM = s->im;
    Cz80_Set_Reg(&CZ80, CZ80_IRQ, s->irq_pending ? ASSERT_LINE : CLEAR_LINE);
    Cz80_Set_IRQ(&CZ80, 0, Cz80_Get_Reg(&CZ80, CZ80_IRQ));
    return 0;
  }
#else
  return 0;
#endif
}

void z80_exit(void)
{
}

void z80_debug(char *dstr)
{
#if defined(_USE_DRZ80)
  sprintf(dstr, "Z80 state: PC: %04x SP: %04x\n", drZ80.Z80PC-drZ80.Z80PC_BASE, drZ80.Z80SP-drZ80.Z80SP_BASE);
#elif defined(_USE_CZ80)
  sprintf(dstr, "Z80 state: PC: %04x SP: %04x\n", (unsigned int)(CZ80.PC - CZ80.BasePC), CZ80.SP.W);
#endif
}

static int
mega_z80_debug_has_breakpoint(u16 addr)
{
  return (mega_z80_debug_breakpoints[addr >> 3] & (u8)(1u << (addr & 7u))) ? 1 : 0;
}

static int
mega_z80_debug_active(void)
{
  return mega_z80_debug_breakpoint_count > 0 ||
    mega_z80_debug_suppressed_breakpoint_active ||
    mega_z80_debug_step_pending;
}

static int
mega_z80_debug_break_if_needed(void)
{
  u16 pc = (u16)z80_pc();

  if (mega_z80_debug_suppressed_breakpoint_active) {
    if (mega_z80_debug_suppressed_breakpoint_addr == pc) {
      mega_z80_debug_suppressed_breakpoint_active = 0;
      return 0;
    }
    mega_z80_debug_suppressed_breakpoint_active = 0;
  }
  if (!mega_z80_debug_has_breakpoint(pc)) {
    return 0;
  }
  mega_z80_debug_suppressed_breakpoint_addr = pc;
  mega_z80_debug_suppressed_breakpoint_active = 1;
  e9k_debug_pause();
  return 1;
}

int
mega_z80_debug_run(int cycles)
{
  int done = 0;

  if (cycles <= 0) {
    return 0;
  }
  if (!mega_z80_debug_active()) {
    return z80_run(cycles);
  }

  while (done < cycles) {
    int ran;

    if (!mega_z80_debug_step_pending && mega_z80_debug_break_if_needed()) {
      break;
    }
    ran = z80_run(1);
    if (ran <= 0) {
      if (mega_z80_debug_step_pending) {
        mega_z80_debug_step_pending = 0;
        e9k_debug_pause();
      }
      break;
    }
    done += ran;
    if (mega_z80_debug_step_pending) {
      mega_z80_debug_step_pending = 0;
      e9k_debug_pause();
      break;
    }
  }

  return done;
}

static void
mega_z80_debug_set_reg(e9k_debug_processor_reg_t *reg, const char *name, u32 value, u8 bits)
{
  memset(reg, 0, sizeof(*reg));
  strncpy(reg->name, name, sizeof(reg->name) - 1);
  reg->value = value;
  reg->bits = bits;
}

static u16
mega_z80_debug_pair(u8 hi, u8 lo)
{
  return (u16)(((u16)hi << 8) | lo);
}

size_t
mega_z80_debug_read_regs(e9k_debug_processor_reg_t *out, size_t cap)
{
  enum { reg_count = 19 };
  struct z80_state state;
  size_t count;
  size_t i = 0;

  if (!out || cap == 0) {
    return 0;
  }

  z80_pack(&state);
  count = reg_count;
  if (count > cap) {
    count = cap;
  }

  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "AF", mega_z80_debug_pair(state.m.a, state.m.f), 16);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "BC", mega_z80_debug_pair(state.m.b, state.m.c), 16);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "DE", mega_z80_debug_pair(state.m.d, state.m.e), 16);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "HL", mega_z80_debug_pair(state.m.h, state.m.l), 16);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "AF'", mega_z80_debug_pair(state.a.a, state.a.f), 16);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "BC'", mega_z80_debug_pair(state.a.b, state.a.c), 16);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "DE'", mega_z80_debug_pair(state.a.d, state.a.e), 16);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "HL'", mega_z80_debug_pair(state.a.h, state.a.l), 16);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "IX", state.ix, 16);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "IY", state.iy, 16);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "SP", state.sp, 16);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "PC", state.pc, 16);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "I", state.i, 8);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "R", state.r, 8);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "IM", state.im, 8);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "IFF1", state.iff1 ? 1u : 0u, 1);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "IFF2", state.iff2 ? 1u : 0u, 1);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "HALT", state.halted ? 1u : 0u, 1);
  }
  if (i < count) {
    mega_z80_debug_set_reg(&out[i++], "BANK68K", Pico.m.z80_bank68k << 15, 24);
  }

  return count;
}

size_t
mega_z80_debug_read_memory(u32 addr, u8 *out, size_t cap)
{
  size_t i;

  if (!out || cap == 0) {
    return 0;
  }
  for (i = 0; i < cap; ++i) {
    out[i] = (u8)z80_read(addr + (u32)i);
  }
  return cap;
}

int
mega_z80_debug_write_memory(u32 addr, u32 value, size_t size)
{
  u16 addr16;

  if (size != 1) {
    return 0;
  }
  addr16 = (u16)addr;
  if (addr16 >= 0x4000u) {
    return 0;
  }
  PicoMem.zram[addr16 & 0x1fffu] = (u8)(value & 0xffu);
  return 1;
}

size_t
mega_z80_debug_disassemble(u32 pc, char *out, size_t cap)
{
  u8 bytes[4];
  size_t i;

  if (!out || cap == 0) {
    return 0;
  }
  for (i = 0; i < sizeof(bytes); ++i) {
    bytes[i] = (u8)z80_read(pc + (u32)i);
  }
  return e9k_z80_dasmDisassemble(bytes, pc, out, cap);
}

void
mega_z80_debug_add_breakpoint(u32 addr)
{
  u16 addr16 = (u16)addr;

  if (mega_z80_debug_has_breakpoint(addr16)) {
    return;
  }
  mega_z80_debug_breakpoints[addr16 >> 3] |= (u8)(1u << (addr16 & 7u));
  mega_z80_debug_breakpoint_count++;
}

void
mega_z80_debug_remove_breakpoint(u32 addr)
{
  u16 addr16 = (u16)addr;

  if (!mega_z80_debug_has_breakpoint(addr16)) {
    return;
  }
  mega_z80_debug_breakpoints[addr16 >> 3] &= (u8)~(u8)(1u << (addr16 & 7u));
  if (mega_z80_debug_breakpoint_count > 0) {
    mega_z80_debug_breakpoint_count--;
  }
  if (mega_z80_debug_suppressed_breakpoint_active &&
      mega_z80_debug_suppressed_breakpoint_addr == addr16) {
    mega_z80_debug_suppressed_breakpoint_active = 0;
  }
}

void
mega_z80_debug_suppress_breakpoint_at_pc(void)
{
  u16 pc = (u16)z80_pc();

  if (!mega_z80_debug_has_breakpoint(pc)) {
    return;
  }
  mega_z80_debug_suppressed_breakpoint_addr = pc;
  mega_z80_debug_suppressed_breakpoint_active = 1;
}

int
mega_z80_debug_step_instruction(void)
{
  mega_z80_debug_step_pending = 1;
  e9k_debug_resume();
  return 1;
}

void
mega_z80_debug_reset(void)
{
  memset(mega_z80_debug_breakpoints, 0, sizeof(mega_z80_debug_breakpoints));
  mega_z80_debug_breakpoint_count = 0;
  mega_z80_debug_suppressed_breakpoint_active = 0;
  mega_z80_debug_step_pending = 0;
}

// vim:ts=2:sw=2:expandtab
