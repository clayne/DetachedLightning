#include "Hooks.h"
#include <xbyak/xbyak.h>

using namespace DetachedLightning;

REL::Relocation<decltype(BeamProjectileHook::m_beamProjectileConstructor)>& BeamProjectileHook::m_getBeamProjectileConstructor() {
  // SE: 0x14074b170+0x185, in the function CreateProjectile_14074b170
  // AE: 0x140779220+0x186, in the function CreateProjectile_140779220

  static REL::Relocation<decltype(m_beamProjectileConstructor)> value(RELOCATION_ID(42928, 44108), RELOCATION_OFFSET(0x185, 0x186));
  return value;
}

void BeamProjectileHook::Hook(SKSE::Trampoline& trampoline) {
  SKSE::log::debug("Starting to hook BeamProjectile::Constructor");
  BeamProjectileHook::m_originalBeamProjectileConstructor
    = trampoline.write_call<5>(BeamProjectileHook::m_getBeamProjectileConstructor().address(), reinterpret_cast<uintptr_t>(BeamProjectileHook::m_beamProjectileConstructor));
  SKSE::log::debug("BeamProjectile constructor hook written");
}

RE::BeamProjectile* BeamProjectileHook::m_beamProjectileConstructor(RE::BeamProjectile* proj, void* launchData) {
  proj = BeamProjectileHook::m_originalBeamProjectileConstructor(proj, launchData);

  // tag fire-and-forget beam spells (mostly lightning bolt spells)
  if (spell) {
    if (spell->GetCastingType() == RE::MagicSystem::CastingType::kFireAndForget) {
      BeamProjectileHook::TagProjectile(proj, 1);
    }
    else {
      BeamProjectileHook::TagProjectile(proj, 0);
    }
  }
  return proj;
}

void NodeHook::Hook(SKSE::Trampoline& trampoline) {
  // This is a weird one. The original code seems to write to the node's x,y,z coordinates,
  // but we want to make that conditional. So we'll insert some asm code that calls into
  // our function which updates the node's position conditionally, and leave behind a jmp
  // to where we actually want to return to, skipping the unconditional coordinate update.

  // This is directly from Fenix's original code:
  // https://github.com/fenix31415/FenixProjectilesAPI/blob/2d41c89e43a49ffdd0c92110d8808acf590fc112/src/Hooks.h#L70
  // which is licensed under the MIT license

  SKSE::log::debug("Trying to hook into the middle of BeamProjectile__UpdateImpl.");

  uintptr_t return_addr = RELOCATION_ID(42568, 43749).address() + RELOCATION_OFFSET(0x2d3, 0x2cf);

  SKSE::log::debug("func_addr: {}, return_addr: {}", uintptr_t(m_moveNode), return_addr);

  struct Code : Xbyak::CodeGenerator
  {
    Code(uintptr_t func_addr, uintptr_t ret_addr)
    {
      Xbyak::Label nocancel;

      // rsi  = proj
      // xmm0 -- xmm2 = node pos
      mov(r9, rsi);        // copy the projectile to a register so it can be read by our function
      mov(rax, func_addr); // put our function's address into rax
      call(rax);           // call our function
      mov(rax, ret_addr);  // put our desired return location into rax
      jmp(rax);            // return to where we specified
    }
  } xbyakCode{ uintptr_t(m_moveNode), return_addr };

  auto codeSize = xbyakCode.getSize();
  auto code = trampoline.allocate(codeSize);
  std::memcpy(code, xbyakCode.getCode(), codeSize);

  trampoline.write_branch<5>(RELOCATION_ID(42568, 43749).address() + RELOCATION_OFFSET(0x2c1, 0x2bd), (uintptr_t)code);
  SKSE::log::debug("Hook into the middle of BeamProjectile__UpdateImpl written.");
}

void NodeHook::m_moveNode(float x, float y, float z, RE::Projectile* proj) {
  auto node = proj->Get3D();

  if (node && BeamProjectileHook::GetTag(proj) != 1) {
    node->local.translate.x = x;
    node->local.translate.y = y;
    node->local.translate.z = z;
  }
}