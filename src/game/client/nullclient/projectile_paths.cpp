#include "projectile_paths.h"

#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/projectile_data.h>

#include <algorithm>
#include <memory>

namespace OpenKrx::NullClient
{
	namespace
	{
		std::vector<CProjectilePath> Trace(CGameWorld &World, int Ticks, const CProjectile *pOnly = nullptr)
		{
			std::vector<CProjectilePath> Paths;
			// Both allocations outlive World, including entity destruction during Tick.
			struct CTracked
			{
				CProjectile *m_pEntity;
				CProjectile::CTraceSample m_Sample;
				bool m_Alive = true;
			};
			std::vector<std::unique_ptr<CTracked>> Tracks;
			for(auto *p = World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); p; p = p->TypeNext())
			{
				auto *pProjectile = static_cast<CProjectile *>(p);
				if(pOnly && pProjectile != pOnly)
					continue;
				const auto Data = pProjectile->GetData();
				const vec2 Position = pProjectile->GetPos((World.GameTick() - pProjectile->GetStartTick()) / 50.0f);
				Paths.push_back({pProjectile->GetOwner(), Data.m_Type, {Position}, false});
				auto Track = std::make_unique<CTracked>();
				Track->m_pEntity = pProjectile;
				Track->m_Sample.m_Position = Position;
				pProjectile->SetTraceObserver(&Track->m_Sample);
				Tracks.push_back(std::move(Track));
			}
			for(int Tick = 0; Tick < std::clamp(Ticks, 0, 200); ++Tick)
			{
				if(std::none_of(Tracks.begin(), Tracks.end(), [](const auto &p) { return p->m_Alive; }))
					break;
				++World.m_GameTick;
				World.Tick();
				for(size_t i = 0; i < Tracks.size(); ++i)
				{
					auto &Track = *Tracks[i];
					if(!Track.m_Alive)
						continue;
					Paths[i].m_vPoints.push_back(Track.m_Sample.m_Position);
					Track.m_Alive = false;
					for(auto *p = World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); p; p = p->TypeNext())
						if(p == Track.m_pEntity && static_cast<CProjectile *>(p)->TraceObserver() == &Track.m_Sample)
						{
							Track.m_Alive = true;
							break;
						}
					Paths[i].m_Impact = !Track.m_Alive && Track.m_Sample.m_Impact;
				}
			}
			// World may outlive this function; never leave pointers to retired samples.
			for(auto *p = World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); p; p = p->TypeNext())
				static_cast<CProjectile *>(p)->SetTraceObserver(nullptr);
			return Paths;
		}
	}
	std::vector<CProjectilePath> ProjectilePaths(const CGameWorld &Source, int Ticks)
	{
		CGameWorld World;
		World.CopyWorldIsolated(&Source);
		World.m_WorldConfig.m_NullPrediction = true;
		World.m_WorldConfig.m_KrxPrediction = false;
		return Trace(World, Ticks);
	}
	std::vector<CProjectilePath> AimProjectilePath(const CGameWorld &Source, int Owner, int Weapon, vec2 Direction, int Ticks)
	{
		if(Weapon != WEAPON_GUN && Weapon != WEAPON_GRENADE && Weapon != WEAPON_SHOTGUN)
			return {};
		CGameWorld World;
		World.CopyWorldIsolated(&Source);
		World.m_WorldConfig.m_NullPrediction = true;
		World.m_WorldConfig.m_KrxPrediction = false;
		auto *pSelf = World.GetCharacterById(Owner);
		if(!pSelf)
			return {};
		Direction = length(Direction) > .001f ? normalize(Direction) : vec2(0, -1);
		const auto *pTune = pSelf->GetTuning(pSelf->GetPureTuneZone());
		const float Lifetime = Weapon == WEAPON_GUN ? pTune->m_GunLifetime : Weapon == WEAPON_GRENADE ? pTune->m_GrenadeLifetime :
														pTune->m_ShotgunLifetime;
		auto *pShot = new CProjectile(&World, Weapon, Owner, pSelf->Core()->m_Pos + Direction * pSelf->GetProximityRadius() * .75f,
			Direction, Lifetime * World.GameTickSpeed(), false, Weapon == WEAPON_GRENADE, -1);
		return Trace(World, Ticks, pShot);
	}
}
