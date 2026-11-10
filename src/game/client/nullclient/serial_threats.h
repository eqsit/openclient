#ifndef GAME_CLIENT_NULLCLIENT_SERIAL_THREATS_H
#define GAME_CLIENT_NULLCLIENT_SERIAL_THREATS_H
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <array>
#include <vector>
class CCollision;
class CGameWorld;
class CGameClient;
namespace OpenKrx::NullClient
{
	// Original 2487b0/247c30: geometric retaliation field, shared by all 36 stand probes.
	class CSerialThreats
	{
		struct CThreat
		{
			vec2 m_Position;
			float m_HammerRadius;
			bool m_Hammer, m_Grenade, m_HookingUs, m_Target, m_OtherHook;
			std::vector<vec2> m_vFuture;
		};
		CCollision *m_pCollision = nullptr;
		std::vector<CThreat> m_vThreats;
		int m_Horizon = 0;
		float m_Range = 0, m_Throw = 0, m_HookLength = 0;
		float HazardDistance(vec2 Position, vec2 Direction, float Range) const;

	public:
		void Build(CGameWorld &World, int Own, int Target, float Range, int Horizon, float Throw, const std::array<bool, MAX_CLIENTS> &Idle, const CGameClient *pClient = nullptr);
		float Risk(vec2 Position) const;
	};
}
#endif
