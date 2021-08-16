#include "platform_macro.h"
#if defined(TARGET_ARCH_X64)

#include "InstructionRelocation/x64/InstructionRelocationX64.h"

#include <string.h>

#include "dobby_internal.h"

#include "InstructionRelocation/x86/x86_insn_decode/x86_insn_decode.h"

#include "core/arch/x64/registers-x64.h"
#include "core/assembler/assembler-x64.h"
#include "core/codegen/codegen-x64.h"

using namespace zz::x64;

static int GenRelocateCodeFixed(void *buffer, AssemblyCode *origin, AssemblyCode *relocated) {
  TurboAssembler turbo_assembler_(0);
  // Set fixed executable code chunk address
  turbo_assembler_.SetRealizedAddress(relocated->begin);
#define _ turbo_assembler_.
#define __ turbo_assembler_.GetCodeBuffer()->

  addr64_t curr_orig_ip = (addr64_t)origin->begin;
  addr64_t curr_relo_ip = (addr64_t)relocated->begin;

  uint8_t *buffer_cursor = (uint8_t *)buffer;

  x86_options_t conf = {0};
  conf.mode = 64;

  int predefined_relocate_size = origin->size;

  while ((buffer_cursor < ((uint8_t *)buffer + predefined_relocate_size))) {
    int last_relo_offset = turbo_assembler_.GetCodeBuffer()->GetBufferSize();

    x86_insn_decode_t insn = {0};
    memset(&insn, 0, sizeof(insn));
    // decode x86 insn
    x86_insn_decode(&insn, (uint8_t *)buffer_cursor, &conf);

    if (insn.primary_opcode >= 0x70 && insn.primary_opcode <= 0x7F) { // jc rel8
      DLOG(0, "[x86 relo] jc rel8, %p", buffer_cursor);

      int8_t orig_offset = insn.immediate;
      int new_offset = (int)(curr_orig_ip + orig_offset - curr_relo_ip);
      uint8_t opcode = 0x80 | (insn.primary_opcode & 0x0f);

      __ Emit8(0x0F);
      __ Emit8(opcode);
      __ Emit32(new_offset);
    } else if (insn.primary_opcode == 0xEB) { // jmp rel8
      DLOG(0, "[x86 relo] jmp rel8, %p", buffer_cursor);

      int8_t orig_offset = insn.immediate;
      int8_t new_offset = (int8_t)(curr_orig_ip + orig_offset - curr_relo_ip);

      __ Emit8(0xE9);
      __ Emit32(new_offset);
    } else if ((insn.flags & X86_INSN_DECODE_FLAG_IP_RELATIVE) && (insn.operands[1].mem.base == RIP)) { // RIP
      DLOG(0, "[x86 relo] rip, %p", buffer_cursor);

      // dword orig_disp = *(dword *)(buffer_cursor + insn.operands[1].mem.disp);
      dword orig_disp = insn.operands[1].mem.disp;
      dword new_disp = (dword)(curr_orig_ip + orig_disp - curr_relo_ip);

      __ EmitBuffer(buffer_cursor, insn.displacement_offset);
      __ Emit32(new_disp);
      if (insn.immediate_offset) {
        __ EmitBuffer((buffer_cursor + insn.immediate_offset), insn.length - insn.immediate_offset);
      }
    } else if (insn.primary_opcode == 0xE8 || insn.primary_opcode == 0xE9) { // call or jmp rel32
      DLOG(0, "[x86 relo] jmp or call rel32, %p", buffer_cursor);

      dword orig_offset = insn.immediate;
      dword offset = (dword)(curr_orig_ip + orig_offset - curr_relo_ip);

      __ EmitBuffer(buffer_cursor, insn.immediate_offset);
      __ Emit32(offset);
    } else if (insn.primary_opcode >= 0xE0 && insn.primary_opcode <= 0xE2) { // LOOPNZ/LOOPZ/LOOP/JECXZ
      // LOOP/LOOPcc
      UNIMPLEMENTED();
    } else if (insn.primary_opcode == 0xE3) {
      // JCXZ JCEXZ JCRXZ
      UNIMPLEMENTED();
    } else {
      // Emit the origin instrution
      __ EmitBuffer(buffer_cursor, insn.length);
    }

    // go next
    curr_orig_ip += insn.length;
    buffer_cursor += insn.length;

#if 0
    {
      // 1 orignal instrution => ? relocated instruction
      int relo_offset = turbo_assembler_.GetCodeBuffer()->GetBufferSize();
      int relo_len    = relo_offset - last_relo_offset;
      curr_relo_ip += relo_len;
    }
#endif
    curr_relo_ip = (addr64_t)relocated->begin + turbo_assembler_.ip_offset();
  }

  // jmp to the origin rest instructions
  CodeGen codegen(&turbo_assembler_);
  // TODO: 6 == jmp [RIP + disp32] instruction size
  addr64_t stub_addr = curr_relo_ip + 6;
  codegen.JmpNearIndirect(stub_addr);
  turbo_assembler_.GetCodeBuffer()->Emit64(curr_orig_ip);

  // update origin
  int new_origin_len = curr_orig_ip - (addr_t)origin->begin;
  origin->size = new_origin_len;

  int relo_len = turbo_assembler_.GetCodeBuffer()->GetBufferSize();
  if (relo_len > relocated->size) {
    DLOG(0, "pre-alloc code chunk not enough");
    return RT_FAILED;
  }

  // Generate executable code
  {
    AssemblyCode *code = nullptr;
    code = AssemblyCodeBuilder::FinalizeFromTurboAssembler(&turbo_assembler_);
    delete code;
  }

  return RT_SUCCESS;
}

void GenRelocateCodeAndBranch(void *buffer, AssemblyCode *origin, AssemblyCode *relocated) {
  int relo_code_chunk_size = 32;
  const int chunk_size_step = 16;

x64_try_again:
  if (relocated->begin == nullptr) {
    auto *block = CodeMemoryArena::SharedInstance()->allocCodeBlock(relo_code_chunk_size);
    if (block == nullptr) {
      return;
    }
    relocated->begin = (void *)block->addr;
    relocated->size = block->size;
  }

  int ret = GenRelocateCodeFixed(buffer, origin, relocated);
  if (ret != RT_SUCCESS) {
    // FIXME: destory

    relo_code_chunk_size += chunk_size_step;
    relocated->begin = 0;
    relocated->size = 0;

    goto x64_try_again;
  }
}

#endif
