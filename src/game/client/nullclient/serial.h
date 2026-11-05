#ifndef GAME_CLIENT_NULLCLIENT_SERIAL_H
#define GAME_CLIENT_NULLCLIENT_SERIAL_H
#include "route.h"
#include "warlist.h"

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <array>
class CGameClient;
namespace OpenKrx::NullClient
{
	class CSerial
	{
		struct CState
		{
			int m_Id = -1, m_Target = -1, m_LastTick = -1, m_Stuck = 0, m_LastRouteTick = -1000, m_Release = 0, m_LastWeapon = -1;
			bool m_Locked = false, m_HasPlan = false, m_HaveVelocity = false;
			vec2 m_Velocity = vec2(0, 0), m_RouteGoal = vec2(0, 0), m_Hazard = vec2(0, 0);
			bool m_HasHazard = false, m_HazardFrozen = false, m_HaveStand = false;
			vec2 m_Stand = vec2(0, 0);
			CRoute m_Route;
			CNetObj_PlayerInput m_Plan{};
		};
		std::array<CState, 2> m_aStates;
		int Pick(CGameClient *pClient, const CWarlist &Warlist, int Slot, int Mode) const;
		void UpdateSlot(CGameClient *pClient, const CWarlist &Warlist, int Slot, const std::array<bool, MAX_CLIENTS> &Idle);

	public:
		void Reset() { m_aStates = {}; }
		void Update(CGameClient *pClient, const CWarlist &Warlist, const std::array<bool, MAX_CLIENTS> &Idle);
		void Lock(CGameClient *pClient, const CWarlist &Warlist);
		int Target(int Slot) const { return m_aStates[Slot].m_Target; }
		bool Locked(int Slot) const { return m_aStates[Slot].m_Locked; }
		CNetObj_PlayerInput Apply(CGameClient *pClient, CNetObj_PlayerInput Input, int Slot, bool Consume = true);
	};
}
#endif
