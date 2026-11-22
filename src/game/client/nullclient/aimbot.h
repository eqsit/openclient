#ifndef GAME_CLIENT_NULLCLIENT_AIMBOT_H
#define GAME_CLIENT_NULLCLIENT_AIMBOT_H
#include <base/vmath.h>
#include <generated/protocol.h>
#include <array>

class CGameClient;
namespace OpenKrx::NullClient
{
struct CBallisticAim { vec2 m_Direction; float m_Ticks; };
// 14020e510. Result order is significant (low flight time first).
int SolveBallistic(vec2 From, vec2 To, float Curvature, float Speed, float TickSpeed, std::array<CBallisticAim, 2> &aResult);
class CAimbot
{
	struct CState { int m_Target = -1, m_StickyTarget = -1, m_LastFire = 0, m_Weapon = -1, m_StickyWeapon = -2; bool m_LastHook = false; };
	std::array<CState, 2> m_aStates{};
public:
	void Reset() { m_aStates = {}; }
	CNetObj_PlayerInput Apply(CGameClient *pClient, CNetObj_PlayerInput Input, int Slot, bool AvoidChanged);
	int Target(int Slot) const { return Slot >= 0 && Slot < 2 ? m_aStates[Slot].m_Target : -1; }
	int Weapon(int Slot) const { return Slot >= 0 && Slot < 2 ? m_aStates[Slot].m_Weapon : -1; }
};
}
#endif
