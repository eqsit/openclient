#include "simulation.h"

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/projectile_data.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
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

		void RestorePredictedAmmo(const CGameWorld &World, CCharacter &Local)
		{
			if(World.m_WorldConfig.m_PredictWeapons)
				for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
					if(Local.GetWeaponGot(Weapon) && Local.GetWeaponAmmo(Weapon) == 0)
						Local.SetWeaponAmmo(Weapon, -1);
		}

		// 4.1, 0x1402819c0 / 0x140281e70: each worker retains its slot.
		// A group using N participants wakes slots 1..N-1 plus the caller (0).
		class CNullPool
		{
			std::mutex m_Mutex;
			std::condition_variable m_Start, m_Done;
			std::vector<std::thread> m_vThreads;
			uint64_t m_Generation = 0;
			bool m_Stop = false;
			int m_Active = 0, m_Pending = 0;
			const std::function<void(int)> *m_pRun = nullptr;

			void Worker(int Slot)
			{
				uint64_t Generation = 0;
				std::unique_lock Lock(m_Mutex);
				while(true)
				{
					m_Start.wait(Lock, [&] { return m_Stop || m_Generation != Generation; });
					if(m_Stop)
						return;
					Generation = m_Generation;
					if(Slot >= m_Active)
						continue;
					const auto *pRun = m_pRun;
					Lock.unlock();
					(*pRun)(Slot);
					Lock.lock();
					if(--m_Pending == 0)
						m_Done.notify_one();
				}
			}

		public:
			CNullPool()
			{
				const unsigned CpuCount = std::thread::hardware_concurrency();
				const unsigned Participants = std::min(CpuCount ? CpuCount : 4u, 16u);
				for(unsigned Slot = 1; Slot < Participants; ++Slot)
					m_vThreads.emplace_back(&CNullPool::Worker, this, Slot);
			}
			~CNullPool()
			{
				{
					std::lock_guard Lock(m_Mutex);
					m_Stop = true;
				}
				m_Start.notify_all();
				for(auto &Thread : m_vThreads)
					Thread.join();
			}
			int Participants() const { return static_cast<int>(m_vThreads.size()) + 1; }

			void Run(int Participants, const std::function<void(int)> &EvaluateSlot)
			{
				{
					std::lock_guard Lock(m_Mutex);
					m_Active = Participants;
					m_Pending = Participants - 1;
					m_pRun = &EvaluateSlot;
					++m_Generation;
				}
				m_Start.notify_all();
				EvaluateSlot(0);
				std::unique_lock Lock(m_Mutex);
				m_Done.wait(Lock, [&] { return m_Pending == 0; });
				m_pRun = nullptr;
			}
		};
		struct CCandidateWork
		{
			std::atomic<int> m_Next{0};
			int m_Count;
			const std::function<void(IWorld &, int)> &m_Evaluate;
			void Run(IWorld &World)
			{
				for(int Index; (Index = m_Next.fetch_add(1, std::memory_order_relaxed)) < m_Count;)
					m_Evaluate(World, Index);
			}
		};

		// One isolated collision per candidate worker, kept for the lifetime of
		// the map. Rebuilding it per search copied every map layer for each
		// worker on every tick; only the cells a candidate writes are journaled
		// and rewound now.
		struct CWorkerCollisionCache
		{
			const CCollision *m_pSource = nullptr;
			std::vector<std::shared_ptr<CCollision>> m_vCopies;
			// Live door layer generation the copies were last synced to; -1
			// forces a resync after invalidation or a map load.
			int m_DoorVersion = -1;
			// The cache is plain unsynchronised memory: it may only be touched
			// from the thread that created it (the game thread).
			std::thread::id m_OwnerThread{};
		};

		CWorkerCollisionCache &WorkerCollisions()
		{
			static CWorkerCollisionCache s_Cache;
			return s_Cache;
		}

		CWorkerCollisionCache &WorkerCollisionsOnOwnerThread()
		{
			auto &Cache = WorkerCollisions();
			const auto Current = std::this_thread::get_id();
			dbg_assert(Cache.m_OwnerThread == std::thread::id() || Cache.m_OwnerThread == Current,
				"worker collision cache touched from a second thread");
			Cache.m_OwnerThread = Current;
			return Cache;
		}

		// Nested dispatch (a worker that evaluates a chained search) must not
		// share the cached contexts, so it falls back to the serial walk.
		std::atomic<int> s_DispatchDepth{0};

	}

	void InvalidateWorkerCollisions()
	{
		auto &Cache = WorkerCollisionsOnOwnerThread();
		Cache.m_pSource = nullptr;
		Cache.m_DoorVersion = -1;
		Cache.m_vCopies.clear();
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

	bool CSimulation::Initialize(const CGameWorld &Source, int ClientId, const CSettings &Settings, const std::array<bool, MAX_CLIENTS> &aIdle, bool Legit)
	{
		const float Radius = Legit ? 320.0f : std::min(static_cast<float>(Settings.m_SimRadius), static_cast<float>(Settings.m_Horizon) * 20.0f + 160.0f);
		const float WeaponRadius = Legit ? 320.0f : Settings.m_Weapons ? std::min(static_cast<float>(Settings.m_WeaponRadius), static_cast<float>(Settings.m_Horizon) * 24.0f + 200.0f) : 0.0f;
		const float PlayerRadius = Legit ? 640.0f : std::min(static_cast<float>(Settings.m_PlayerRadius), static_cast<float>(Settings.m_Horizon) * 20.0f + 580.0f);
		return Initialize(Source, ClientId, Settings, aIdle, CPruneRadii{Radius, WeaponRadius, PlayerRadius});
	}

	bool CSimulation::Initialize(const CGameWorld &Source, int ClientId, const CSettings &Settings, const std::array<bool, MAX_CLIENTS> &aIdle, CPruneRadii Radii)
	{
		m_vWorkerContexts.clear();
		m_pCounters = std::make_shared<CCounters>();
		m_ClientId = ClientId;
		m_aIdle = aIdle;
		m_Root.CopyWorldIsolated(&Source);
		++m_pCounters->m_Copies;
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
		RestorePredictedAmmo(m_Root, *pLocal);
		Prune(Radii);
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

	bool CSimulation::InitializePathliner(const CGameWorld &Source, int ClientId)
	{
		m_ClientId = ClientId;
		m_World.CopyWorldIsolated(&Source);
		m_World.m_WorldConfig.m_NullPrediction = true;
		m_World.m_WorldConfig.m_KrxPrediction = false;
		m_Adversarial = 0;
		m_Steps = 0;
		auto *pLocal = Character();
		if(!pLocal)
			return false;
		RestorePredictedAmmo(m_World, *pLocal);
		return true;
	}

	void CSimulation::Prune(CPruneRadii Radii)
	{
		auto *pLocal = m_Root.GetCharacterById(m_ClientId);
		if(!pLocal)
			return;
		const vec2 Origin = pLocal->GetPos();
		for(int Type = 0; Type < CGameWorld::NUM_ENTTYPES; ++Type)
			for(auto *pEntity = m_Root.FindFirst(Type); pEntity;)
			{
				auto *pNext = pEntity->TypeNext();
				bool Remove = false;
				if(pEntity != pLocal)
				{
					if(Type == CGameWorld::ENTTYPE_PICKUP)
						Remove = true;
					else if(Type == CGameWorld::ENTTYPE_PROJECTILE)
					{
						auto *pProjectile = static_cast<CProjectile *>(pEntity);
						const auto Data = pProjectile->GetData();
						const int Age = std::max(0, m_Root.GameTick() - Data.m_StartTick);
						const float Range = std::max(Radii.m_General, Radii.m_Weapons);
						Remove = (Data.m_Type == WEAPON_GUN && !Data.m_Explosive && !Data.m_Freeze) ||
							distance(pProjectile->GetPos(static_cast<float>(Age) / m_Root.GameTickSpeed()), Origin) > Range;
					}
					else if(Type == CGameWorld::ENTTYPE_CHARACTER && static_cast<CCharacter *>(pEntity)->Core()->HookedPlayer() == m_ClientId)
						Remove = false;
					else
					{
						const float Range = Type < 2 ? std::max(Radii.m_General, Radii.m_Weapons) : Type == CGameWorld::ENTTYPE_CHARACTER ? std::max(Radii.m_General, Radii.m_Players) :
																			Radii.m_General;
						const float Distance = distance(pEntity->GetPos(), Origin);
						Remove = Distance > Range;
						if(Type == CGameWorld::ENTTYPE_CHARACTER && Distance > 320.0f)
						{
							auto *pCharacter = static_cast<CCharacter *>(pEntity);
							const auto *pCore = pCharacter->Core();
							if(m_aIdle[pCharacter->GetId()] || FrozenThreat(pCharacter))
							{
								if(dot(pCore->m_Vel, pCore->m_Vel) <= 0.000001f && pCore->m_HookState < HOOK_RETRACT_START && pCharacter->IsGrounded())
									Remove = true;
							}
							else if(pCharacter->GetActiveWeapon() != WEAPON_GRENADE && pCore->m_HookState != HOOK_FLYING &&
								m_Root.Collision()->IntersectLine(Origin, pEntity->GetPos(), nullptr, nullptr))
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
		m_vRocketContactThreats.clear();
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
			if(Settings.m_RocketContact && pOther->GetActiveWeapon() == WEAPON_GRENADE &&
				(Hooker || (Settings.m_RocketContactMode == 1 && !m_aIdle[Id])))
				m_vRocketContactThreats.emplace_back(Id, pOther->GetProximityRadius() * 1.25f + pLocal->GetProximityRadius());
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
		if(pCopy->m_pCollisionOwner && pCopy->m_pCollisionOwner->HasChangeJournal())
		{
			// The clone shares the journaled map. Remember where the parent's
			// writes end so only the clone's own cells are undone.
			pCopy->m_JournalTileMark = pCopy->m_pCollisionOwner->JournalTileMark();
		}
		pCopy->m_pTuningOwner = m_pTuningOwner;
		pCopy->m_pCounters = m_pCounters;
		pCopy->m_Root.CopyWorldIsolated(&m_World);
		++pCopy->m_pCounters->m_Copies;
		pCopy->m_ClientId = m_ClientId;
		pCopy->m_Adversarial = m_Adversarial;
		pCopy->m_aIdle = m_aIdle;
		pCopy->m_vHammerThreats = m_vHammerThreats;
		pCopy->m_vRocketContactThreats = m_vRocketContactThreats;
		pCopy->m_vRocketThreats = m_vRocketThreats;
		pCopy->m_RocketNear = m_RocketNear;
		pCopy->Reset();
		return pCopy;
	}
	std::unique_ptr<CSimulation> CSimulation::CloneForWorker(const std::shared_ptr<CCollision> &pCollision) const
	{
		auto pCopy = std::make_unique<CSimulation>();
		pCopy->m_pCollisionOwner = pCollision;
		pCopy->m_JournalTileMark = 0;
		pCopy->m_pTuningOwner = std::make_shared<std::array<CTuningParams, 256>>();
		std::copy_n(m_World.TuningList(), 256, pCopy->m_pTuningOwner->begin());
		pCopy->m_Root.CopyWorldIsolated(&m_Root);
		++pCopy->m_pCounters->m_Copies;
		pCopy->m_Root.RebindCollision(pCopy->m_pCollisionOwner.get());
		pCopy->m_Root.RebindTuning(pCopy->m_pTuningOwner->data());
		pCopy->m_ClientId = m_ClientId;
		pCopy->m_Adversarial = m_Adversarial;
		pCopy->m_aIdle = m_aIdle;
		pCopy->m_vHammerThreats = m_vHammerThreats;
		pCopy->m_vRocketContactThreats = m_vRocketContactThreats;
		pCopy->m_vRocketThreats = m_vRocketThreats;
		pCopy->m_RocketNear = m_RocketNear;
		pCopy->Reset();
		return pCopy;
	}
	void CSimulation::ForEachCandidate(int Count, const std::function<void(IWorld &, int)> &Evaluate)
	{
		// 0x140276d90 only dispatches with at least 12 eligible moves, requesting
		// min(pool participants, count/4). Selection/ties remain on the caller.
		if(Count < 12)
		{
			IWorld::ForEachCandidate(Count, Evaluate);
			return;
		}
		static CNullPool Pool;
		const int Participants = std::min(Pool.Participants(), Count / 4);
		if(Participants < 2)
		{
			IWorld::ForEachCandidate(Count, Evaluate);
			return;
		}
		CCollision *pSource = m_World.Collision();
		if(!pSource)
		{
			IWorld::ForEachCandidate(Count, Evaluate);
			return;
		}
		// A chained search evaluated inside a worker must not reuse the cached
		// contexts; the serial walk keeps its own state and stays correct.
		struct CDispatchGuard
		{
			bool m_Owner;
			CDispatchGuard() : m_Owner(s_DispatchDepth.fetch_add(1, std::memory_order_acq_rel) == 0) {}
			~CDispatchGuard() { s_DispatchDepth.fetch_sub(1, std::memory_order_acq_rel); }
		} Guard;
		if(!Guard.m_Owner)
		{
			IWorld::ForEachCandidate(Count, Evaluate);
			return;
		}
		auto &Cache = WorkerCollisionsOnOwnerThread();
		if(Cache.m_pSource != pSource)
		{
			// The client reuses one CCollision object across maps, so callers
			// also invalidate this cache explicitly on map load.
			Cache.m_pSource = pSource;
			Cache.m_vCopies.clear();
		}
		while(static_cast<int>(Cache.m_vCopies.size()) < Participants - 1)
		{
			std::shared_ptr<CCollision> pCopy(pSource->CreateIsolatedCopy().release());
			pCopy->EnableChangeJournal();
			Cache.m_vCopies.push_back(std::move(pCopy));
		}
		// Every candidate starts from the map as it is now: undo the cells the
		// previous search left behind, then pull in doors the live prediction
		// moved since the copies were created.
		for(auto &pCopy : Cache.m_vCopies)
			pCopy->RewindJournalTo(0);
		if(Cache.m_DoorVersion != pSource->DoorVersion())
		{
			bool Synced = true;
			for(auto &pCopy : Cache.m_vCopies)
				Synced = pCopy->CopyDoorsFrom(*pSource) && Synced;
			// Only claim the copies are current if every one of them synced.
			if(Synced)
				Cache.m_DoorVersion = pSource->DoorVersion();
		}
		// Reuse private snapshots between priority groups sharing this root.
		Reset();
		while(static_cast<int>(m_vWorkerContexts.size()) < Participants - 1)
		{
			auto pWorker = CloneForWorker(Cache.m_vCopies[m_vWorkerContexts.size()]);
			m_pCounters->m_Copies += pWorker->TotalCopies();
			m_vWorkerContexts.push_back(std::move(pWorker));
		}
		CCandidateWork Work{{0}, Count, Evaluate};
		std::array<CCounters, 16> aWork{};
		Pool.Run(Participants, [&](int Slot) {
			CSimulation &World = Slot == 0 ? *this : *m_vWorkerContexts[Slot - 1];
			const CCounters Before = *World.m_pCounters;
			Work.Run(World);
			aWork[Slot] = {World.TotalSteps() - Before.m_Steps, World.TotalCopies() - Before.m_Copies};
		});
		for(int Slot = 1; Slot < Participants; ++Slot)
		{
			m_pCounters->m_Steps += aWork[Slot].m_Steps;
			m_pCounters->m_Copies += aWork[Slot].m_Copies;
		}
	}
	void CSimulation::Reset()
	{
		RewindCollision();
		m_World.CopyWorldIsolated(&m_Root);
		++m_pCounters->m_Copies;
		m_Steps = 0;
	}
	void CSimulation::RewindCollision()
	{
		if(m_pCollisionOwner && m_pCollisionOwner->HasChangeJournal())
			m_pCollisionOwner->RewindJournalTo(m_JournalTileMark);
	}
	CSimulation::~CSimulation()
	{
		// A clone shares the parent's journaled map; leave it exactly as found.
		RewindCollision();
	}
	void CSimulation::CommitRoot()
	{
		m_vWorkerContexts.clear();
		m_Root.CopyWorldIsolated(&m_World);
		++m_pCounters->m_Copies;
		m_Steps = 0;
	}
	CCharacter *CSimulation::Character() const { return const_cast<CGameWorld &>(m_World).GetCharacterById(m_ClientId); }
	bool CSimulation::Valid() const { return Character() != nullptr; }
	bool CSimulation::Frozen() const
	{
		const auto *p = Character();
		return p && (FrozenThreat(p) || p->Core()->m_LiveFrozen);
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
	bool CSimulation::GroundedOnFreeze() const
	{
		if(!Grounded())
			return false;
		const auto *pCollision = m_World.Collision();
		const int Index = pCollision->GetPureMapIndex(Position());
		return pCollision->GetTileIndex(Index) == TILE_FREEZE || pCollision->GetFrontTileIndex(Index) == TILE_FREEZE;
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
				// 0x140285ea8 sets the hook field before the 120-unit hammer test.
				Other.m_Hook = 1;
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
					Other.m_WantedWeapon = WEAPON_HAMMER + 1;
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
		++m_pCounters->m_Steps;
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

	bool CSimulation::RocketContactDanger() const
	{
		if(!Valid())
			return false;
		const vec2 Origin = Position();
		for(const auto &[Id, Radius] : m_vRocketContactThreats)
			if(const auto *pCharacter = const_cast<CGameWorld &>(m_World).GetCharacterById(Id); pCharacter && distance(pCharacter->GetPos(), Origin) < Radius)
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
		auto &Root = const_cast<CGameWorld &>(m_Root);
		auto *pLocal = Root.GetCharacterById(m_ClientId);
		if(!pLocal || m_RocketNear <= 0 || Ticks < 1)
			return -1;
		int Result = -1;
		const float Gravity = pLocal->GetTuning(pLocal->GetPureTuneZone())->m_Gravity;
		const vec2 Origin = pLocal->GetPos();
		for(const auto &[Id, Near] : m_vRocketThreats)
		{
			const auto *p = Root.GetCharacterById(Id);
			if(!p)
				continue;
			vec2 Point = p->GetPos(), Velocity = p->Core()->m_Vel;
			if(distance(Point, Origin) <= m_RocketNear)
				return 0;
			for(int Step = 1; Step <= Ticks; ++Step)
			{
				const vec2 Previous = Point;
				Velocity.y += Gravity;
				Point.x += Velocity.x;
				Point.y += Velocity.y;
				if(Root.Collision()->IntersectLine(Previous, Point, nullptr, nullptr))
					break;
				if(distance(Point, Origin) <= m_RocketNear)
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
