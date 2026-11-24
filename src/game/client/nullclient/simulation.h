#ifndef GAME_CLIENT_NULLCLIENT_SIMULATION_H
#define GAME_CLIENT_NULLCLIENT_SIMULATION_H

#include "policy.h"
#include "settings.h"

#include <game/client/prediction/gameworld.h>

#include <array>

class CCharacter;
class CGameClient;

namespace OpenKrx::NullClient
{
	// 14023e890/14023e860: unchanged aim and attack counter with no direction,
	// measured in authoritative game ticks, marks an idle tee after 150 ticks.
	class CIdleTracker
	{
		int m_LastTick = -1;
		std::array<int, MAX_CLIENTS> m_aAngles{}, m_aAttacks{}, m_aTicks{};
		std::array<bool, MAX_CLIENTS> m_aSeen{};

	public:
		void Reset() { *this = {}; }
		void Update(CGameClient *pClient);
		std::array<bool, MAX_CLIENTS> Idle() const;
	};

	// OpenKRX/DDNet integration: resolve a switch already present in the previous
	// predicted input, through the actual direct-input handler in an isolated copy.
	int WeaponOnDirectInput(const CGameWorld &Source, int ClientId, const CNetObj_PlayerInput &Input);

	// Parallel candidate workers keep one isolated collision snapshot per worker
	// for the lifetime of the map; only cells written by a candidate are
	// journaled and rewound. Call on map load so snapshots never outlive a map.
	void InvalidateWorkerCollisions();

	struct CPruneRadii
	{
		float m_General;
		float m_Weapons;
		float m_Players;
	};

	class CSimulation final : public IWorld
	{
		// These owners precede the worlds so their map/tuning remain alive
		// through every entity destructor and nested chain checkpoint.
		std::shared_ptr<CCollision> m_pCollisionOwner;
		std::shared_ptr<std::array<CTuningParams, 256>> m_pTuningOwner;
		struct CCounters
		{
			uint64_t m_Steps = 0, m_Copies = 0;
		};
		std::shared_ptr<CCounters> m_pCounters = std::make_shared<CCounters>();
		CGameWorld m_Root;
		CGameWorld m_World;
		int m_ClientId = -1;
		int m_Adversarial = 0;
		int m_Steps = 0;
		std::array<bool, MAX_CLIENTS> m_aIdle{};
		std::vector<std::pair<int, float>> m_vHammerThreats;
		std::vector<std::pair<int, float>> m_vRocketContactThreats;
		std::vector<std::pair<int, bool>> m_vRocketThreats;
		std::vector<std::unique_ptr<CSimulation>> m_vWorkerContexts;
		float m_RocketNear = 0;
		// Journal position of this world on its isolated collision copy; every
		// reset/clone returns to it instead of re-copying the map.
		int m_JournalTileMark = 0;
		void Prune(CPruneRadii Radii);
		void PrepareThreats(const CSettings &Settings);
		void RewindCollision();
		std::unique_ptr<CSimulation> CloneForWorker(const std::shared_ptr<CCollision> &pCollision) const;

	public:
		~CSimulation() override;
		bool InitializePathliner(const CGameWorld &Source, int ClientId);
		bool Initialize(const CGameWorld &Source, int ClientId, const CSettings &Settings, const std::array<bool, MAX_CLIENTS> &aIdle, bool Legit = false);
		bool Initialize(const CGameWorld &Source, int ClientId, const CSettings &Settings, const std::array<bool, MAX_CLIENTS> &aIdle, CPruneRadii Radii);
		std::unique_ptr<IWorld> Clone() const override;
		void ForEachCandidate(int Count, const std::function<void(IWorld &, int)> &Evaluate) override;
		void Reset() override;
		bool Tick(const CNetObj_PlayerInput &Input) override;
		bool Valid() const override;
		bool Dead() const override;
		bool Frozen() const override;
		bool DeepFrozen() const override;
		bool Grounded() const override;
		bool GroundedOnFreeze() const override;
		vec2 Position() const override;
		vec2 Velocity() const override;
		bool HammerDanger() const override;
		bool RocketDanger(float Throw, float Spread) const override;
		bool RocketContactDanger() const override;
		bool NearFreeze(float Clearance) const override;
		bool WallStopped() const override;
		bool HookedPlayer() const override;
		float HazardRay(vec2 Position, vec2 Velocity) const override;

		CCharacter *Character() const;
		CGameWorld &World() { return m_World; }
		void CommitRoot();
		int Steps() const { return m_Steps; }
		uint64_t TotalSteps() const { return m_pCounters->m_Steps; }
		uint64_t TotalCopies() const { return m_pCounters->m_Copies; }
		const std::vector<std::pair<int, float>> &HammerThreats() const { return m_vHammerThreats; }
		const std::vector<std::pair<int, float>> &RocketContactThreats() const { return m_vRocketContactThreats; }
		int HammerThreatCount() const { return static_cast<int>(m_vHammerThreats.size()); }
		int RocketThreatCount() const { return static_cast<int>(m_vRocketThreats.size()); }
		int IncomingHooker(vec2 *pPosition = nullptr) const;
		std::optional<vec2> NearestCharacter(float Radius) const;
		std::optional<vec2> NearestRocketThreat() const;
		int RocketApproach(int Ticks) const;
		bool HasProjectiles() const;
		bool HasOwnedProjectile() const;
	};
}

#endif
