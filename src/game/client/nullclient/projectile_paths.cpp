#include "projectile_paths.h"
#include "simulation.h"

#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/laser.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/projectile_data.h>

#include <game/collision.h>

#include <algorithm>
#include <memory>

namespace OpenKrx::NullClient
{
	namespace
	{
		template<typename F>
		std::vector<CProjectilePath> Trace(CGameWorld &World, int Ticks, F Advance, const CProjectile *pOnly = nullptr)
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
				const vec2 Position = pProjectile->GetPos(std::max(0.0f, (World.GameTick() - pProjectile->GetStartTick()) / 50.0f));
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
				if(!Advance())
					break;
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
		struct CShotSample
		{
			int m_Id;
			int m_Owner;
			int m_Weapon;
			vec2 m_Position;
			bool m_Used;
		};

		void SampleShots(CGameWorld &World, std::vector<CShotSample> &Samples)
		{
			Samples.clear();
			for(auto *p = World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); p; p = p->TypeNext())
			{
				auto *pShot = static_cast<CProjectile *>(p);
				const float Age = std::max(0, World.GameTick() - pShot->GetStartTick()) / 50.0f;
				Samples.push_back({pShot->GetId(), pShot->GetOwner(), pShot->GetData().m_Type, pShot->GetPos(Age), false});
			}
		}

		template<typename F>
		std::vector<CProjectilePath> TrackSimulation(CGameWorld &World, int Ticks, F Advance)
		{
			std::vector<CShotSample> Samples;
			SampleShots(World, Samples);
			std::vector<int> Ids;
			std::vector<CProjectilePath> Paths;
			for(const auto &Shot : Samples)
			{
				Ids.push_back(Shot.m_Id);
				Paths.push_back({Shot.m_Owner, Shot.m_Weapon, {Shot.m_Position}});
			}
			for(int Tick = 0; !Paths.empty() && Tick < std::clamp(Ticks, 0, 200) && Advance(); ++Tick)
			{
				SampleShots(World, Samples);
				for(size_t Index = 0; Index < Paths.size(); ++Index)
				{
					auto &Path = Paths[Index];
					if(Path.m_Lost)
						continue;
					CShotSample *pBest = nullptr;
					float BestDistance = 400.0f;
					for(auto &Shot : Samples)
					{
						if(Shot.m_Used || Shot.m_Id != Ids[Index])
							continue;
						const float D = distance(Path.m_vPoints.back(), Shot.m_Position);
						if(D < BestDistance || (!pBest && D == BestDistance))
						{
							pBest = &Shot;
							BestDistance = D;
						}
					}
					if(pBest)
					{
						pBest->m_Used = true;
						Path.m_vPoints.push_back(pBest->m_Position);
					}
					else
						Path.m_Lost = true;
				}
			}
			return Paths;
		}

	}
	std::vector<CProjectilePath> ProjectilePaths(const CGameWorld &Source, int Ticks)
	{
		CGameWorld World;
		World.CopyWorldIsolated(&Source);
		World.m_WorldConfig.m_NullPrediction = true;
		World.m_WorldConfig.m_KrxPrediction = false;
		return TrackSimulation(World, Ticks, [&] { ++World.m_GameTick; World.Tick(); return true; });
	}
	std::vector<CProjectilePath> ProjectilePaths(CSimulation &Sim, const CNetObj_PlayerInput &Input, int Ticks)
	{
		Sim.Reset();
		auto Paths = TrackSimulation(Sim.World(), Ticks, [&] { return Sim.Tick(Input); });
		Sim.Reset();
		return Paths;
	}

	ColorRGBA ProjectileColor(int Weapon)
	{
		switch(Weapon)
		{
		case WEAPON_GUN: return {.85f, .85f, .95f, 1};
		case WEAPON_SHOTGUN: return {1, .75f, .35f, 1};
		case WEAPON_GRENADE: return {.45f, 1, .65f, 1};
		case WEAPON_LASER: return {.6f, .6f, 1, 1};
		default: return {1, 1, 1, 1};
		}
	}

	std::vector<CProjectilePath> DetailedProjectilePaths(CGameWorld &World)
	{
		std::vector<CProjectilePath> Paths;
		for(auto *p = World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); p; p = p->TypeNext())
		{
			auto *pShot = static_cast<CProjectile *>(p);
			const float Age = std::max(0, World.GameTick() - pShot->GetStartTick()) / 50.0f;
			auto &Path = Paths.emplace_back(CProjectilePath{pShot->GetOwner(), pShot->GetData().m_Type, {pShot->GetPos(Age)}});
			for(int Step = 1; Step <= 90; ++Step)
			{
				vec2 Next = pShot->GetPos(Age + Step * .01f), Hit;
				if(World.Collision() && World.Collision()->IntersectLine(Path.m_vPoints.back(), Next, &Hit, nullptr))
				{
					Path.m_vPoints.push_back(Hit);
					Path.m_Impact = true;
					break;
				}
				Path.m_vPoints.push_back(Next);
			}
		}
		return Paths;
	}

	std::vector<vec2> VisibleAimPath(const CGameWorld &World, vec2 Position, vec2 Direction, int Owner, int Weapon, int Zone, float Radius, int Ticks, std::span<const CAimTee> Tees)
	{
		const auto *pCollision = World.Collision();
		if(!pCollision || Weapon < WEAPON_GUN || Weapon > WEAPON_LASER)
			return {};
		const auto &Tune = *World.GetTuning(Zone);
		std::vector<vec2> Points;
		if(Weapon == WEAPON_LASER || (Weapon == WEAPON_SHOTGUN && World.m_WorldConfig.m_IsDDRace))
		{
			vec2 From = Position;
			Points.push_back(From);
			float Reach = Tune.m_LaserReach;
			for(int Bounce = 0; Bounce <= 24 && Reach > 0; ++Bounce)
			{
				vec2 Hit, Before;
				const int Solid = pCollision->IntersectLineTeleWeapon(From, From + Direction * Reach, &Hit, &Before);
				Points.push_back(Hit);
				if(!Solid)
					break;
				vec2 Reflected = Direction * 4.0f;
				pCollision->MovePoint(&Before, &Reflected, 1.0f, nullptr);
				const float Travel = distance(From, Before);
				if(Travel < .001f || length(Reflected) < .001f)
					break;
				Reach -= Travel + World.GlobalTuning()->m_LaserBounceCost;
				From = Before;
				Direction = normalize(Reflected);
			}
		}
		else
		{
			const vec2 Start = Position + Direction * (Radius * .75f);
			const float Curvature = Weapon == WEAPON_GUN ? Tune.m_GunCurvature : Weapon == WEAPON_GRENADE ? Tune.m_GrenadeCurvature : Tune.m_ShotgunCurvature;
			const float Speed = Weapon == WEAPON_GUN ? Tune.m_GunSpeed : Weapon == WEAPON_GRENADE ? Tune.m_GrenadeSpeed : Tune.m_ShotgunSpeed;
			Points.push_back(Start);
			for(int Step = 1; Step <= 2 * std::clamp(Ticks, 0, 200); ++Step)
			{
				const vec2 Next = CalcPos(Start, Direction, Curvature, Speed, Step / 100.0f);
				vec2 Hit;
				if(pCollision->IntersectLine(Points.back(), Next, &Hit, nullptr))
				{
					Points.push_back(Hit);
					break;
				}
				Points.push_back(Next);
			}
		}
		vec2 aIntersections[2];
		if(Points.size() >= 2 && distance(Points.front(), Position) < Radius &&
			intersect_line_circle(Points[0], Points[1], Position, Radius, aIntersections) == 2 &&
			distance(Points[0], aIntersections[1]) <= distance(Points[0], Points[1]))
			Points[0] = aIntersections[1];
		for(size_t I = 1; I < Points.size(); ++I)
		{
			const vec2 From = Points[I - 1], Delta = Points[I] - From;
			const float Len2 = length_squared(Delta);
			if(Len2 < 1e-6f)
				continue;
			float First = 2.0f;
			for(const auto &Tee : Tees)
			{
				if(Tee.m_Id == Owner)
				{
					if(I == 1)
						continue;
					if(distance(From, Position) < Radius)
					{
						First = 0;
						break;
					}
				}
				if(!intersect_line_circle(From, Points[I], Tee.m_Position, Radius, aIntersections))
					continue;
				const float Entry = dot(aIntersections[0] - From, Delta) / Len2;
				if(Entry > .001f && Entry <= 1.0f && Entry < First)
					First = Entry;
			}
			if(First <= 1.0f)
			{
				Points.resize(I);
				Points.push_back(From + Delta * First);
				break;
			}
		}
		return Points;
	}

	std::vector<CProjectilePath> AimProjectilePath(const CGameWorld &Source, int Owner, int Weapon, vec2 Direction, int Ticks)
	{
		if(Weapon != WEAPON_GUN && Weapon != WEAPON_GRENADE && Weapon != WEAPON_SHOTGUN && Weapon != WEAPON_LASER)
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
		if(Weapon == WEAPON_LASER || (Weapon == WEAPON_SHOTGUN && World.m_WorldConfig.m_IsDDRace))
		{
			const auto *pLaser = new CLaser(&World, pSelf->Core()->m_Pos, Direction, pTune->m_LaserReach, Owner, Weapon);
			std::vector<CProjectilePath> vSegments;
			int LastEval = -1;
			for(int Tick = 0; Tick < std::clamp(Ticks, 0, 200); ++Tick)
			{
				auto *pEntity = World.FindFirst(CGameWorld::ENTTYPE_LASER);
				while(pEntity && pEntity != pLaser)
					pEntity = pEntity->TypeNext();
				if(!pEntity)
					break;
				if(pLaser->GetEvalTick() != LastEval)
				{
					vSegments.push_back({Owner, Weapon, {pLaser->GetFrom(), pLaser->GetPos()}, false});
					LastEval = pLaser->GetEvalTick();
				}
				++World.m_GameTick;
				World.Tick();
			}
			return vSegments;
		}
		const float Lifetime = Weapon == WEAPON_GUN ? pTune->m_GunLifetime : Weapon == WEAPON_GRENADE ? pTune->m_GrenadeLifetime :
														pTune->m_ShotgunLifetime;
		auto *pShot = new CProjectile(&World, Weapon, Owner, pSelf->Core()->m_Pos + Direction * pSelf->GetProximityRadius() * .75f,
			Direction, Lifetime * World.GameTickSpeed(), false, Weapon == WEAPON_GRENADE, -1);
		return Trace(World, Ticks, [&] { ++World.m_GameTick; World.Tick(); return true; }, pShot);
	}
}
