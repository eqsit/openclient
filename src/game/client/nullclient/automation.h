#ifndef GAME_CLIENT_NULLCLIENT_AUTOMATION_H
#define GAME_CLIENT_NULLCLIENT_AUTOMATION_H
#include <base/vmath.h>
#include <engine/shared/protocol.h>
#include <generated/protocol.h>
#include <array>
#include <optional>

namespace OpenKrx::NullClient
{
// Original 140213690 state +0x18..+0x27. Character observations are supplied
// separately so the original instructions can be the fixture's oracle.
struct CAutomationState
{
	int m_Hooked = -1;
	int m_ReleaseTicks = 0, m_RehookTicks = 0, m_RehookTarget = -1;
	void Apply(CNetObj_PlayerInput &Input, int Hooked, vec2 Position,
		const std::array<std::optional<vec2>, MAX_CLIENTS> &aTargets,
		bool AutoFire, bool AutoRehook, int RehookTicks, bool Ready, bool Ammo,
		bool AvoidChanged, bool AvoidFire)
	{
		if(AutoFire && !AvoidFire && (Input.m_Fire & 1) && Ready && Ammo)
			Input.m_Fire = static_cast<int>(static_cast<unsigned>(Input.m_Fire) + 2U);
		if(!AvoidChanged && AutoRehook && Input.m_Hook)
		{
			if(m_Hooked >= 0 && Hooked < 0)
			{
				m_ReleaseTicks = 1;
				m_RehookTicks = RehookTicks;
				m_RehookTarget = m_Hooked;
			}
			else if(Hooked >= 0)
				m_RehookTicks = m_ReleaseTicks = 0, m_RehookTarget = -1;
		}
		else
			m_RehookTicks = m_ReleaseTicks = 0, m_RehookTarget = -1;
		if(m_ReleaseTicks > 0)
		{
			Input.m_Hook = 0;
			--m_ReleaseTicks;
		}
		else if(m_RehookTicks > 0 && m_RehookTarget >= 0 && m_RehookTarget < MAX_CLIENTS)
		{
			if(aTargets[m_RehookTarget])
			{
				const vec2 Delta = *aTargets[m_RehookTarget] - Position;
				if(length(Delta) > 0.001f)
				{
					Input.m_TargetX = static_cast<int>(Delta.x);
					Input.m_TargetY = static_cast<int>(Delta.y);
				}
				Input.m_Hook = 1;
				--m_RehookTicks;
			}
			else
				m_RehookTicks = 0, m_RehookTarget = -1;
		}
		m_Hooked = Hooked;
	}
};
static_assert(sizeof(CAutomationState) == 16);
}
#endif
