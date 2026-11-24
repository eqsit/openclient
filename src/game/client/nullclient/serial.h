#ifndef GAME_CLIENT_NULLCLIENT_SERIAL_H
#define GAME_CLIENT_NULLCLIENT_SERIAL_H
#include "route.h"
#include "warlist.h"

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <array>
#include <optional>
class CGameClient;
namespace OpenKrx::NullClient
{
	class CSerial
	{
		struct CState
		{
			int m_LastUpdateTick = -1;
			int m_Id = -1, m_Target = -1, m_LastTick = -1, m_Stuck = 0, m_LastRouteTick = -1000, m_Release = 0, m_LastWeapon = -1, m_LastJump = 0;
			bool m_Locked = false, m_HasPlan = false, m_HaveVelocity = false;
			vec2 m_Velocity = vec2(0, 0), m_RouteGoal = vec2(0, 0), m_Hazard = vec2(0, 0);
			bool m_HasHazard = false, m_HazardFrozen = false, m_HaveStand = false;
			vec2 m_Stand = vec2(0, 0);
			CRoute m_Route;
			CNetObj_PlayerInput m_Plan{};
			int m_NavigationTick = -1000;
			int m_NavigationHook = 0;
			vec2 m_NavigationGoal = vec2(0, 0);
			CNetObj_PlayerInput m_NavigationInput{};
		};
		std::array<CState, 2> m_aStates;
		int Pick(CGameClient *pClient, const CWarlist &Warlist, int Slot, int Mode) const;
		void UpdateSlot(CGameClient *pClient, const CWarlist &Warlist, int Slot, const std::array<bool, MAX_CLIENTS> &Idle);
		void Navigate(CGameClient *pClient, CState &State, vec2 Goal, vec2 Target, const class CSerialThreats &Threats, bool OpenAhead, const std::array<bool, MAX_CLIENTS> &Idle);

	public:
		void Reset() { m_aStates = {}; }
		void Update(CGameClient *pClient, const CWarlist &Warlist, const std::array<bool, MAX_CLIENTS> &Idle);
		void Lock(CGameClient *pClient, const CWarlist &Warlist);
		static std::optional<vec2> FindHookAnchor(CCollision *pCollision, vec2 Position, vec2 Delta, float HookLength);
		int Target(int Slot) const { return m_aStates[Slot].m_Target; }
		bool Locked(int Slot) const { return m_aStates[Slot].m_Locked; }
		const CNetObj_PlayerInput *Plan(int Slot) const { return HasPlan(Slot) ? &m_aStates[Slot].m_Plan : nullptr; }
		bool HasPlan(int Slot) const { return m_aStates[Slot].m_HasPlan; }
		CNetObj_PlayerInput Apply(CGameClient *pClient, CNetObj_PlayerInput Input, int Slot) const;
	};
}
#endif
