#ifndef GAME_CLIENT_NULLCLIENT_OUTPUT_H
#define GAME_CLIENT_NULLCLIENT_OUTPUT_H

#include "settings.h"

#include <generated/protocol.h>

#include <array>

namespace OpenKrx::NullClient
{
	// State consumed by 14020a420. Deciding and emitting are distinct operations:
	// emission preserves live fire, weapon and player-flag fields except an
	// explicit weapon action, and can run repeatedly during one prediction tick.
	struct COutputState
	{
		CNetObj_PlayerInput m_Plan{}; // +36b0
		bool m_HasPlan = false; // +36d8
		int m_WeaponAction = -1; // +370c, consumed once by emission
		int m_RestoreWeapon = -1; // +3710
		int m_CutTicks = 0; // +7f10
		unsigned m_CutMask = 0; // +7f14: hook1/jump2/direction4
		int m_HookTicks = 0; // +7f1c
		int m_HookX = 0, m_HookY = 0; // +7f20/24
		bool m_UseHookAim = false; // +7f28, false while hook already grabbed
		std::array<bool, 3> m_aVetoDirections{}; // +7f41..43
		bool m_VetoJump = false, m_VetoHook = false, m_VetoFire = false;
		unsigned m_FireCounter = 0; // +7f48
		bool m_HasVeto = false; // +7f4c

		CNetObj_PlayerInput Apply(CNetObj_PlayerInput Input, bool Enabled, bool CorrectSlot, bool HasCharacter, int ActiveWeapon, const CSettings &Settings);
	};
}

#endif
