#ifndef GAME_CLIENT_NULLCLIENT_AIMBOT_H
#define GAME_CLIENT_NULLCLIENT_AIMBOT_H
#include <base/vmath.h>
#include <generated/protocol.h>
#include <array>

class CGameClient;
namespace OpenKrx::NullClient
{
struct CBallisticAim { vec2 m_Direction; float m_Ticks; };
// Result order is significant (low flight time first).
int SolveBallistic(vec2 From, vec2 To, float Curvature, float Speed, float TickSpeed, std::array<CBallisticAim, 2> &aResult);
class CAimbot
{
	enum class EStatus { CLEAR, AVOID, INACTIVE_WEAPON, BUSY, NOBODY };
	struct CState
	{
		int m_Target = -1, m_StickyTarget = -1, m_LastFire = 0, m_Weapon = -1, m_StickyWeapon = -2;
		bool m_LastHook = false;
		EStatus m_Status = EStatus::CLEAR;
		int m_Seen = 0, m_Far = 0, m_Behind = 0, m_NoShot = 0;
	};
	// Controls sends the currently selected tee through one aimbot instance.
	CState m_State;
public:
	void Reset() { m_State = {}; }
	CNetObj_PlayerInput Apply(CGameClient *pClient, CNetObj_PlayerInput Input, int Slot, bool AvoidChanged);
	int Target() const { return m_State.m_Target; }
	int Weapon() const { return m_State.m_Weapon; }
	bool DebugMessage(char *pBuffer, int Size) const;
};
}
#endif
