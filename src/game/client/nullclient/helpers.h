#ifndef GAME_CLIENT_NULLCLIENT_HELPERS_H
#define GAME_CLIENT_NULLCLIENT_HELPERS_H

#include "automation.h"
#include <base/vmath.h>
#include <generated/protocol.h>
#include <array>

class CGameClient;

namespace OpenKrx::NullClient
{
class CHelpers
{
	struct CSlot
	{
		CAutomationState m_Automation;
		int m_GrenadeTick = -1, m_GrenadeDelay = -1, m_GrenadeTarget = -1;
		bool m_HasVelocity = false;
		vec2 m_Velocity = vec2(0, 0);
		int m_FakeFire = 0, m_FakeTick = -1;
		float m_FakeAngle = 0;
		vec2 m_RealAim = vec2(1, 0), m_SentAim = vec2(1, 0);
		bool m_HasFakeAim = false;
	};
	std::array<CSlot, 2> m_aSlots{};
	int m_LastKillTick = 0;

public:
	void Reset() { *this = {}; }
	CNetObj_PlayerInput Apply(CGameClient *pClient, CNetObj_PlayerInput Input, int Slot, bool AvoidChanged, bool AvoidFire, bool AvoidHookAim);
	void Update(CGameClient *pClient);
	bool VisibleAim(int Slot, vec2 &Aim) const;
	bool ForceSend(int Slot) const { return Slot >= 0 && Slot < 2 && m_aSlots[Slot].m_HasFakeAim; }
};
}
#endif
