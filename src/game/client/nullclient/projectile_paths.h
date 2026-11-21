#ifndef GAME_CLIENT_NULLCLIENT_PROJECTILE_PATHS_H
#define GAME_CLIENT_NULLCLIENT_PROJECTILE_PATHS_H
#include <base/vmath.h>

#include <vector>
class CGameWorld;
class CNetObj_PlayerInput;
namespace OpenKrx::NullClient
{
	class CSimulation;
	struct CProjectilePath
	{
		int m_Owner, m_Weapon;
		std::vector<vec2> m_vPoints;
		bool m_Impact = false;
	};
	// 14020d5f0: advance one shared copied world, following each live shell.
	std::vector<CProjectilePath> ProjectilePaths(const CGameWorld &Source, int Ticks);
	std::vector<CProjectilePath> ProjectilePaths(CSimulation &Simulation, const CNetObj_PlayerInput &Input, int Ticks);
	std::vector<CProjectilePath> AimProjectilePath(const CGameWorld &Source, int Owner, int Weapon, vec2 Direction, int Ticks);
}
#endif
