#include "simulation.h"

#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/projectile_data.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <future>
#include <thread>

namespace OpenKrx::NullClient
{
	namespace
	{
		bool FreezeTile(int Tile) { return Tile == TILE_FREEZE || Tile == TILE_DFREEZE; }
		bool FrozenThreat(const CCharacter *pCharacter)
		{
			return pCharacter->m_FreezeTime > 0 || pCharacter->Core()->m_DeepFrozen;
		}

		// Null 243cd0/243f90: persistent private pool, up to 16 participants
		// including the caller, dynamic candidate claims, synchronous completion.
		struct CNullPool
		{
			const int m_Participants;
			CJobPool m_Jobs;
			CNullPool() : m_Participants(std::clamp(static_cast<int>(std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 4), 1, 16))
			{
				m_Jobs.Init(m_Participants - 1);
			}
		};
		struct CCandidateWork
		{
			std::atomic<int> m_Next{0};
			int m_Count;
			std::function<void(IWorld &, int)> m_Evaluate;
			void Run(IWorld &World)
			{
				for(int Index; (Index = m_Next.fetch_add(1, std::memory_order_relaxed)) < m_Count;)
					m_Evaluate(World, Index);
			}
		};
		class CCandidateJob final : public IJob
		{
			std::unique_ptr<CSimulation> m_pWorld;
			std::shared_ptr<CCandidateWork> m_pWork;
			std::promise<uint64_t> m_Completed;
			void Run() override
			{
				m_pWork->Run(*m_pWorld);
				const auto Steps = m_pWorld->TotalSteps();
				m_pWorld.reset();
				m_Completed.set_value(Steps);
			}

		public:
			CCandidateJob(std::unique_ptr<CSimulation> pWorld, std::shared_ptr<CCandidateWork> pWork) : m_pWorld(std::move(pWorld)), m_pWork(std::move(pWork)) {}
			std::future<uint64_t> Result() { return m_Completed.get_future(); }
		};
	}

	void CIdleTracker::Update(CGameClient *pClient)
	{
		const int Tick = pClient->Client()->GameTick(g_Config.m_ClDummy);
		if(Tick == m_LastTick)
			return;
		const int Delta = m_LastTick < 0 ? 1 : std::max(1, Tick - m_LastTick);
		m_LastTick = Tick;
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		{
			const auto &Character = pClient->m_Snap.m_aCharacters[Id];
			if(!Character.m_Active)
			{
				m_aSeen[Id] = false;
				m_aTicks[Id] = 0;
				continue;
			}
			const auto &Input = Character.m_Cur;
			if(m_aSeen[Id] && Input.m_Direction == 0 && Input.m_Angle == m_aAngles[Id] && Input.m_AttackTick == m_aAttacks[Id])
				m_aTicks[Id] += Delta;
			else
				m_aTicks[Id] = 0;
			m_aAngles[Id] = Input.m_Angle;
			m_aAttacks[Id] = Input.m_AttackTick;
			m_aSeen[Id] = true;
		}
	}

	std::array<bool, MAX_CLIENTS> CIdleTracker::Idle() const
	{
		std::array<bool, MAX_CLIENTS> Result{};
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			Result[Id] = m_aSeen[Id] && m_aTicks[Id] > 149;
		return Result;
	}

	int WeaponOnDirectInput(const CGameWorld &Source, int ClientId, const CNetObj_PlayerInput &Input)
	{
		const auto *pSource = const_cast<CGameWorld &>(Source).GetCharacterById(ClientId);
		if(!pSource)
			return -1;
		// No switch in flight needs no copy. New WantedWeapon alone must not
		// be treated as ready: DDNet HandleWeaponSwitch reads m_Input's previous
		// wanted weapon, while OnDirectInput has only updated m_LatestInput.
		const int Active = pSource->GetActiveWeapon();
		const int Pending = pSource->DiagnosticCurrentInput().m_WantedWeapon;
		if(pSource->DiagnosticReloadTimer() != 0 ||
			((Pending <= 0 || Pending == Active + 1) && pSource->DiagnosticQueuedWeapon() < 0 &&
				Input.m_NextWeapon == pSource->DiagnosticLatestInput().m_NextWeapon &&
				Input.m_PrevWeapon == pSource->DiagnosticLatestInput().m_PrevWeapon))
			return Active;
		CGameWorld Probe;
		Probe.CopyWorldIsolated(&Source);
		auto *pCharacter = Probe.GetCharacterById(ClientId);
		pCharacter->OnDirectInput(&Input);
		return pCharacter->GetActiveWeapon();
	}

	bool CSimulation::Initialize(const CGameWorld &Source, int ClientId, const CSettings &Settings, const std::array<bool, MAX_CLIENTS> &aIdle)
	{
		m_pTotalSteps = std::make_shared<uint64_t>(0);
		m_ClientId = ClientId;
		m_aIdle = aIdle;
		m_Root.CopyWorldIsolated(&Source);
		m_Root.m_WorldConfig.m_NullPrediction = true;
		m_Root.m_WorldConfig.m_KrxPrediction = false;
		m_Root.m_WorldConfig.m_KrxAvoidDeath = false;
		m_Root.m_WorldConfig.m_KrxAvoidTeles = false;
		// 1401f6410 enables freeze/weapon prediction for active godmode even when
		// the ordinary DDNet AntiPing options are off. Keep that policy in this
		// isolated world as well as the live integration's world-config update.
		if(m_Root.m_WorldConfig.m_PredictFreeze == 0)
			m_Root.m_WorldConfig.m_PredictFreeze = 1;
		if(Settings.m_Weapons)
			m_Root.m_WorldConfig.m_PredictWeapons = true;
		auto *pLocal = m_Root.GetCharacterById(ClientId);
		if(!pLocal)
			return false;
		// 247320 repairs zero ammo only when weapon prediction is enabled.
		if(m_Root.m_WorldConfig.m_PredictWeapons)
			for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
				if(pLocal->GetWeaponGot(Weapon) && pLocal->GetWeaponAmmo(Weapon) == 0)
					pLocal->SetWeaponAmmo(Weapon, -1);
		Prune(Settings);
		m_Adversarial = 0;
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			if(Id != ClientId && m_Root.GetCharacterById(Id))
			{
				m_Adversarial = Settings.m_Adversarial;
				break;
			}
		Reset();
		PrepareThreats(Settings);
		return Valid();
	}

	void CSimulation::Prune(const CSettings &Settings)
	{
		auto *pLocal = m_Root.GetCharacterById(m_ClientId);
		if(!pLocal)
			return;
		const vec2 Origin = pLocal->GetPos();
		const float Radius = std::min(static_cast<float>(Settings.m_SimRadius), static_cast<float>(Settings.m_Horizon) * 20.0f + 160.0f);
		const float WeaponRadius = Settings.m_Weapons ? std::min(static_cast<float>(Settings.m_WeaponRadius), static_cast<float>(Settings.m_Horizon) * 24.0f + 200.0f) : 0;
		const float PlayerRadius = std::min(static_cast<float>(Settings.m_PlayerRadius), static_cast<float>(Settings.m_Horizon) * 20.0f + 580.0f);
		for(int Type = 0; Type < CGameWorld::NUM_ENTTYPES; ++Type)
			for(auto *pEntity = m_Root.FindFirst(Type); pEntity;)
			{
				auto *pNext = pEntity->TypeNext();
				bool Remove = false;
				if(pEntity != pLocal)
				{
					if(Type == CGameWorld::ENTTYPE_PICKUP)
						Remove = true;
					else if(Type == CGameWorld::ENTTYPE_PROJECTILE && static_cast<CProjectile *>(pEntity)->GetData().m_Type == WEAPON_SHOTGUN)
						Remove = true;
					else if(Type == CGameWorld::ENTTYPE_CHARACTER && static_cast<CCharacter *>(pEntity)->Core()->HookedPlayer() == m_ClientId)
						Remove = false;
					else
					{
						const float Range = Type < 2 ? std::max(Radius, WeaponRadius) : Type == CGameWorld::ENTTYPE_CHARACTER ? std::max(Radius, PlayerRadius) :
																			Radius;
						const float Distance = distance(pEntity->GetPos(), Origin);
						Remove = Distance > Range;
						if(Type == CGameWorld::ENTTYPE_CHARACTER && Distance > 320.0f)
						{
							const auto *pCharacter = static_cast<CCharacter *>(pEntity);
							if(m_aIdle[pCharacter->GetId()] || FrozenThreat(pCharacter))
								Remove = true;
							else if(pCharacter->GetActiveWeapon() != WEAPON_GRENADE && m_Root.Collision()->IntersectLine(Origin, pEntity->GetPos(), nullptr, nullptr))
								Remove = true;
						}
					}
					if(Remove)
						delete pEntity;
				}
				pEntity = pNext;
			}
	}

	void CSimulation::PrepareThreats(const CSettings &Settings)
	{
		m_vHammerThreats.clear();
		m_vRocketThreats.clear();
		m_RocketNear = 0;
		auto *pLocal = m_Root.GetCharacterById(m_ClientId);
		if(!pLocal)
			return;
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		{
			auto *pOther = m_Root.GetCharacterById(Id);
			if(Id == m_ClientId || !pOther || FrozenThreat(pOther))
				continue;
			const bool Hooker = pOther->Core()->HookedPlayer() == m_ClientId;
			if(Settings.m_HammerDodge && !pOther->HammerHitDisabled() &&
				(Settings.m_HammerDodgeMode == 0 ? Hooker : (m_aIdle[Id] ? Hooker : Hooker || pOther->GetActiveWeapon() == WEAPON_HAMMER)))
				m_vHammerThreats.emplace_back(Id, pOther->GetProximityRadius() * 1.25f + pLocal->GetProximityRadius());
			if(Settings.m_RocketDodge && !m_aIdle[Id] && pOther->GetActiveWeapon() == WEAPON_GRENADE && distance(pOther->GetPos(), pLocal->GetPos()) <= Settings.m_RocketRange)
			{
				bool Near = distance(pOther->GetPos(), pLocal->GetPos()) <= Settings.m_RocketNear;
				if(!Near && Settings.m_RocketLookahead > 0)
				{
					vec2 Position = pOther->GetPos(), Velocity = pOther->Core()->m_Vel;
					const float Gravity = pLocal->GetTuning(pLocal->GetPureTuneZone())->m_Gravity;
					for(int Step = 1; Step <= Settings.m_RocketLookahead; ++Step)
					{
						const vec2 Previous = Position;
						Position.x += Velocity.x;
						Velocity.y += Gravity;
						Position.y += Velocity.y;
						if(m_Root.Collision()->IntersectLine(Previous, Position, nullptr, nullptr))
							break;
						if(distance(Position, pLocal->GetPos()) <= Settings.m_RocketNear)
						{
							Near = true;
							break;
						}
					}
				}
				m_vRocketThreats.emplace_back(Id, Near);
				m_RocketNear = std::max(m_RocketNear, static_cast<float>(Settings.m_RocketNear));
			}
		}
	}

	std::unique_ptr<IWorld> CSimulation::Clone() const
	{
		auto pCopy = std::make_unique<CSimulation>();
		pCopy->m_pCollisionOwner = m_pCollisionOwner;
		pCopy->m_pTuningOwner = m_pTuningOwner;
		pCopy->m_pTotalSteps = m_pTotalSteps;
		pCopy->m_Root.CopyWorldIsolated(&m_World);
		pCopy->m_ClientId = m_ClientId;
		pCopy->m_Adversarial = m_Adversarial;
		pCopy->m_aIdle = m_aIdle;
		pCopy->m_vHammerThreats = m_vHammerThreats;
		pCopy->m_vRocketThreats = m_vRocketThreats;
		pCopy->m_RocketNear = m_RocketNear;
		pCopy->Reset();
		return pCopy;
	}
	std::unique_ptr<CSimulation> CSimulation::CloneForWorker() const
	{
		auto pCopy = std::unique_ptr<CSimulation>(static_cast<CSimulation *>(Clone().release()));
		pCopy->m_pTotalSteps = std::make_shared<uint64_t>(0);
		pCopy->m_pCollisionOwner = m_World.Collision()->CreateIsolatedCopy();
		pCopy->m_pTuningOwner = std::make_shared<std::array<CTuningParams, 256>>();
		std::copy_n(m_World.TuningList(), 256, pCopy->m_pTuningOwner->begin());
		pCopy->m_Root.RebindCollision(pCopy->m_pCollisionOwner.get());
		pCopy->m_Root.RebindTuning(pCopy->m_pTuningOwner->data());
		pCopy->Reset();
		return pCopy;
	}
	void CSimulation::ForEachCandidate(int Count, const std::function<void(IWorld &, int)> &Evaluate)
	{
		// 23b810 only dispatches with at least 12 eligible moves, requesting
		// min(pool participants, count/4). Selection/ties remain on the caller.
		if(Count < 12)
		{
			IWorld::ForEachCandidate(Count, Evaluate);
			return;
		}
		static CNullPool Pool;
		const int Participants = std::min(Pool.m_Participants, Count / 4);
		if(Participants < 2)
		{
			IWorld::ForEachCandidate(Count, Evaluate);
			return;
		}
		auto pWork = std::make_shared<CCandidateWork>();
		pWork->m_Count = Count;
		pWork->m_Evaluate = Evaluate;
		std::vector<std::shared_ptr<CCandidateJob>> vJobs;
		std::vector<std::future<uint64_t>> vResults;
		// Finish every snapshot before the caller starts mutating its world.
		Reset();
		for(int Worker = 1; Worker < Participants; ++Worker)
		{
			auto pJob = std::make_shared<CCandidateJob>(CloneForWorker(), pWork);
			vResults.push_back(pJob->Result());
			vJobs.push_back(std::move(pJob));
		}
		for(auto &pJob : vJobs)
			Pool.m_Jobs.Add(pJob);
		pWork->Run(*this);
		// Drain every job before captured candidate arrays, weights, and input
		// go out of scope. The client is compiled without C++ exceptions.
		for(auto &Result : vResults)
			*m_pTotalSteps += Result.get();
	}
	void CSimulation::Reset()
	{
		m_World.CopyWorldIsolated(&m_Root);
		m_Steps = 0;
	}
	void CSimulation::CommitRoot()
	{
		m_Root.CopyWorldIsolated(&m_World);
		m_Steps = 0;
	}
	CCharacter *CSimulation::Character() const { return const_cast<CGameWorld &>(m_World).GetCharacterById(m_ClientId); }
	bool CSimulation::Valid() const { return Character() != nullptr; }
	bool CSimulation::Frozen() const
	{
		const auto *p = Character();
		return p && (p->m_FreezeTime > 0 || p->Core()->m_DeepFrozen || p->Core()->m_LiveFrozen);
	}
	bool CSimulation::DeepFrozen() const
	{
		const auto *p = Character();
		return p && p->Core()->m_DeepFrozen;
	}
	bool CSimulation::Grounded() const
	{
		auto *p = Character();
		return p && p->IsGrounded();
	}
	vec2 CSimulation::Position() const
	{
		const auto *p = Character();
		return p ? p->GetPos() : vec2(0, 0);
	}
	vec2 CSimulation::Velocity() const
	{
		const auto *p = Character();
		return p ? p->Core()->m_Vel : vec2(0, 0);
	}
	bool CSimulation::Dead() const
	{
		auto *p = Character();
		if(!p)
			return true;
		if(p->Core()->m_Super || p->Core()->m_Invincible)
			return false;
		if(p->GameLayerClipped(p->GetPos()))
			return true;
		const auto *pCollision = m_World.Collision();
		const int Index = pCollision->GetPureMapIndex(p->GetPos());
		return pCollision->GetTileIndex(Index) == TILE_DEATH || pCollision->GetFrontTileIndex(Index) == TILE_DEATH;
	}

	bool CSimulation::Tick(const CNetObj_PlayerInput &Input)
	{
		auto *pLocal = Character();
		if(!pLocal)
			return false;
		if(m_Adversarial)
		{
			const bool PredictWeapons = m_World.m_WorldConfig.m_PredictWeapons;
			m_World.m_WorldConfig.m_PredictWeapons = true;
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			{
				auto *pOther = m_World.GetCharacterById(Id);
				if(Id == m_ClientId || !pOther || pOther->Core()->m_HookHitDisabled)
					continue;
				if(m_Adversarial != 2 && pOther->Core()->HookedPlayer() != m_ClientId && pOther->Core()->m_HookState != HOOK_FLYING)
					continue;
				const vec2 Delta = pLocal->GetPos() - pOther->GetPos();
				const float Distance = length(Delta);
				if(Distance < 0.001f)
					continue;
				CNetObj_PlayerInput Other{};
				Other.m_PlayerFlags = PLAYERFLAG_PLAYING;
				Other.m_Direction = Delta.x > 0 ? 1 : Delta.x < 0 ? -1 :
										    0;
				Other.m_TargetX = static_cast<int>(Delta.x);
				Other.m_TargetY = static_cast<int>(Delta.y);
				if(!Other.m_TargetX && !Other.m_TargetY)
					Other.m_TargetX = 1;
				if(Distance < 120.0f && !pOther->HammerHitDisabled())
				{
					pOther->GiveWeapon(WEAPON_HAMMER);
					pOther->SetWeaponAmmo(WEAPON_HAMMER, -1);
					pOther->SetWeapon(WEAPON_HAMMER);
					Other.m_Hook = 1;
					Other.m_Fire = m_Steps + 1;
				}
				pOther->OnDirectInput(&Other);
				pOther->OnPredictedInput(&Other);
			}
			m_World.m_WorldConfig.m_PredictWeapons = PredictWeapons;
		}
		pLocal->OnDirectInput(&Input);
		++m_World.m_GameTick;
		pLocal->OnPredictedInput(&Input);
		m_World.Tick();
		++m_Steps;
		++*m_pTotalSteps;
		return Valid();
	}

	bool CSimulation::HammerDanger() const
	{
		if(!Valid())
			return false;
		for(const auto &[Id, Radius] : m_vHammerThreats)
			if(const auto *p = const_cast<CGameWorld &>(m_World).GetCharacterById(Id); p && distance(p->GetPos(), Position()) < Radius)
				return true;
		return false;
	}

	bool CSimulation::RocketDanger(float Throw, float Spread) const
	{
		if(!Valid() || Throw <= 0)
			return false;
		const float Fan = (std::max(0.0f, Spread) * 0.5f * 3.1415927f) / 180.0f;
		const auto *pCollision = m_World.Collision();
		for(const auto &[Id, Near] : m_vRocketThreats)
		{
			const auto *p = const_cast<CGameWorld &>(m_World).GetCharacterById(Id);
			if(!Near || !p)
				continue;
			const vec2 Delta = Position() - p->GetPos();
			if(length(Delta) < 0.001f)
				return true;
			const float Center = std::atan2(Delta.y, Delta.x);
			for(int Ray = 0; Ray < 5; ++Ray)
			{
				const float Angle = ((Fan + Fan) * static_cast<float>(Ray) * 0.25f - Fan) + Center;
				const vec2 Direction(std::cos(Angle), std::sin(Angle));
				int PreviousIndex = -1;
				for(float Range = 12.0f; Range <= Throw; Range += 12.0f)
				{
					const vec2 Point = Position() + Direction * Range;
					const int Index = pCollision->GetPureMapIndex(Point);
					if(Index == PreviousIndex)
						continue;
					if(pCollision->CheckPoint(round_to_int(Point.x), round_to_int(Point.y)))
						break;
					const int Tile = pCollision->GetTileIndex(Index), Front = pCollision->GetFrontTileIndex(Index);
					if(FreezeTile(Tile) || FreezeTile(Front) || Tile == TILE_DEATH || Front == TILE_DEATH)
						return true;
					PreviousIndex = Index;
				}
			}
		}
		return false;
	}

	bool CSimulation::NearFreeze(float Clearance) const
	{
		if(!Valid() || Clearance <= 0)
			return false;
		static constexpr vec2 s_aDirections[] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.707f, 0.707f}, {-0.707f, 0.707f}, {0.707f, -0.707f}, {-0.707f, -0.707f}};
		const auto *pCollision = m_World.Collision();
		for(vec2 Direction : s_aDirections)
		{
			const vec2 Point = Position() + Direction * Clearance;
			if(pCollision->CheckPoint(round_to_int(Point.x), round_to_int(Point.y)))
				continue;
			const int Index = pCollision->GetPureMapIndex(Point);
			if(FreezeTile(pCollision->GetTileIndex(Index)) || FreezeTile(pCollision->GetFrontTileIndex(Index)))
				return true;
		}
		return false;
	}
	bool CSimulation::WallStopped() const
	{
		const auto *p = Character();
		return p && p->Core()->m_Colliding != 0;
	}
	bool CSimulation::HookedPlayer() const
	{
		const auto *p = Character();
		return p && p->Core()->HookedPlayer() >= 0;
	}
	float CSimulation::HazardRay(vec2 Position, vec2 Velocity) const
	{
		const float Speed = length(Velocity);
		if(Speed < 2.0f)
			return 0;
		const float Range = std::min(480.0f, Speed * 14.0f);
		const int Steps = std::max(1, static_cast<int>(Range * 0.0625f));
		const auto *pCollision = m_World.Collision();
		for(int Step = 1; Step <= Steps; ++Step)
		{
			const vec2 Point = Position + (Velocity / Speed) * ((static_cast<float>(Step) / static_cast<float>(Steps)) * Range);
			if(pCollision->CheckPoint(round_to_int(Point.x), round_to_int(Point.y)))
				return 0;
			const int Index = pCollision->GetPureMapIndex(Point);
			const int Tile = pCollision->GetTileIndex(Index), Front = pCollision->GetFrontTileIndex(Index);
			if(FreezeTile(Tile) || FreezeTile(Front) || Tile == TILE_DEATH || Front == TILE_DEATH)
				return (1.0f - static_cast<float>(Step) / static_cast<float>(Steps)) * 200000.0f;
		}
		return 0;
	}

	int CSimulation::IncomingHooker(vec2 *pPosition) const
	{
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			if(const auto *p = const_cast<CGameWorld &>(m_World).GetCharacterById(Id); Id != m_ClientId && p && p->Core()->HookedPlayer() == m_ClientId)
			{
				if(pPosition)
					*pPosition = p->GetPos();
				return Id;
			}
		return -1;
	}
	std::optional<vec2> CSimulation::NearestCharacter(float Radius) const
	{
		std::optional<vec2> Result;
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			if(const auto *p = const_cast<CGameWorld &>(m_World).GetCharacterById(Id); Id != m_ClientId && p && distance(Position(), p->GetPos()) <= Radius)
			{
				Radius = distance(Position(), p->GetPos());
				Result = p->GetPos();
			}
		return Result;
	}
	std::optional<vec2> CSimulation::NearestRocketThreat() const
	{
		std::optional<vec2> Result;
		float Distance = 0;
		for(const auto &[Id, Near] : m_vRocketThreats)
			if(const auto *p = const_cast<CGameWorld &>(m_World).GetCharacterById(Id); p && (!Result || distance(Position(), p->GetPos()) < Distance))
			{
				Result = p->GetPos();
				Distance = distance(Position(), p->GetPos());
			}
		return Result;
	}
	int CSimulation::RocketApproach(int Ticks) const
	{
		if(!Valid() || m_RocketNear <= 0 || Ticks < 1)
			return -1;
		int Result = -1;
		const float Gravity = Character()->GetTuning(Character()->GetPureTuneZone())->m_Gravity;
		for(const auto &[Id, Near] : m_vRocketThreats)
		{
			const auto *p = const_cast<CGameWorld &>(m_Root).GetCharacterById(Id);
			if(!p)
				continue;
			vec2 Point = p->GetPos(), Velocity = p->Core()->m_Vel;
			if(distance(Point, Position()) <= m_RocketNear)
				return 0;
			for(int Step = 1; Step <= Ticks; ++Step)
			{
				const vec2 Previous = Point;
				Velocity.y += Gravity;
				Point.x += Velocity.x;
				Point.y += Velocity.y;
				if(m_World.Collision()->IntersectLine(Previous, Point, nullptr, nullptr))
					break;
				if(distance(Point, Position()) <= m_RocketNear)
				{
					if(Result < 0 || Step < Result)
						Result = Step;
					break;
				}
			}
		}
		return Result;
	}
	bool CSimulation::HasProjectiles() const
	{
		auto &World = const_cast<CGameWorld &>(m_World);
		return World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE) || World.FindFirst(CGameWorld::ENTTYPE_LASER);
	}
	bool CSimulation::HasOwnedProjectile() const
	{
		for(auto *p = static_cast<CProjectile *>(const_cast<CGameWorld &>(m_World).FindFirst(CGameWorld::ENTTYPE_PROJECTILE)); p; p = static_cast<CProjectile *>(p->TypeNext()))
			if(p->GetOwner() == m_ClientId)
				return true;
		return false;
	}
}
