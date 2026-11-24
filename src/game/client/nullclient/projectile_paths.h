#ifndef GAME_CLIENT_NULLCLIENT_PROJECTILE_PATHS_H
#define GAME_CLIENT_NULLCLIENT_PROJECTILE_PATHS_H
#include <base/vmath.h>
#include <base/color.h>
#include <span>

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
		bool m_Lost = false;
	};
	struct CAimTee
	{
		int m_Id;
		vec2 m_Position;
	};
	ColorRGBA ProjectileColor(int Weapon);
	std::vector<CProjectilePath> DetailedProjectilePaths(CGameWorld &World);
	std::vector<vec2> VisibleAimPath(const CGameWorld &World, vec2 Position, vec2 Direction, int Owner, int Weapon, int Zone, float Radius, int Ticks, std::span<const CAimTee> Tees);
	std::vector<CProjectilePath> ProjectilePaths(const CGameWorld &Source, int Ticks);
	std::vector<CProjectilePath> ProjectilePaths(CSimulation &Simulation, const CNetObj_PlayerInput &Input, int Ticks);
	std::vector<CProjectilePath> AimProjectilePath(const CGameWorld &Source, int Owner, int Weapon, vec2 Direction, int Ticks);
}
#endif
