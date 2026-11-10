#ifndef GAME_CLIENT_NULLCLIENT_POLICY_H
#define GAME_CLIENT_NULLCLIENT_POLICY_H

#include <base/vmath.h>

#include <generated/protocol.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class CCollision;

namespace OpenKrx::NullClient
{
	class CRoute;
	// Layout and semantics recovered from NullClient 3.6, 1402395c0/14023b3f0.
	// Keep these fields in binary order to support instruction-level fixtures.
	struct CMove
	{
		int m_Direction = 0;
		int m_ReverseAt = 0;
		int m_Jump = 0;
		int m_HookMode = 0;
		vec2 m_HookAim = vec2(1, 0);
		bool m_ReleaseHookFirst = false;
		int m_Duration = 0;
		int m_ReleaseHookAt = 0;
		int m_WeaponAction = -1;
		vec2 m_WeaponAim = vec2(1, 0);
		int m_WantedWeapon = -1;
		int m_FireAt = 0;
	};
	static_assert(sizeof(CMove) == 0x38);

	struct CWeights
	{
		vec2 m_Anchor = vec2(0, 0);
		float m_Hold = 0;
		float m_HoldSlack = 0;
		float m_Settle = 0;
		vec2 m_Start = vec2(0, 0);
		float m_Travel = 0;
		bool m_HandBack = false;
		bool m_RespectHeld = false;
		const CRoute *m_pReservedFlyPath = nullptr; // Original +0x28; Avoid supplies null.
		const CNetObj_PlayerInput *m_pManual = nullptr;
		float m_Deviation = 0;
		const CNetObj_PlayerInput *m_pPrevious = nullptr;
		float m_Continuity = 0;
		float m_FreezeCost = 0;
		float m_Clearance = 0;
		float m_HammerCost = 0;
		float m_RocketCost = 0;
		float m_RocketThrow = 0;
		float m_RocketSpread = 0;
		float m_CounterPull = 0;
		float m_CounterY = 0;
		bool m_CounterBelow = false;
		bool m_FreezeFatal = false;
		int m_Chain = 0;
		int m_Coast = 0;
		float m_ReleaseHookCost = 0;
		float m_AirJump = 0;
		float m_GroundJump = 0;
		float m_Commitment = 0;
		float m_HookCost = 0;
		int m_HookAge = 0;
		float m_HookAgeCost = 0;
		float m_WallCost = 0;
		float m_TeeHookCost = 0;
		float m_HazardSpeed = 0;
	};
	static_assert(sizeof(CWeights) == 0xa0);

	struct CEvaluation
	{
		int m_FailureTick = -1;
		int m_ManualDifference = 0;
		int m_PreviousDifference = 0;
		bool m_SafeEnd = true;
		float m_Cost = 0;
		bool m_Pruned = false;
	};
	static_assert(sizeof(CEvaluation) == 0x18);

	// Observations are taken after each original prediction-world tick.
	class IWorld
	{
	public:
		virtual ~IWorld() = default;
		virtual std::unique_ptr<IWorld> Clone() const = 0;
		// The native world supplies the original parallel evaluator. Scripted
		// instruction fixtures keep the same deterministic sequential oracle.
		virtual void ForEachCandidate(int Count, const std::function<void(IWorld &, int)> &Evaluate)
		{
			for(int Index = 0; Index < Count; ++Index)
				Evaluate(*this, Index);
		}
		virtual void Reset() = 0;
		virtual bool Tick(const CNetObj_PlayerInput &Input) = 0;
		virtual bool Valid() const = 0;
		virtual bool Dead() const = 0;
		virtual bool Frozen() const = 0;
		virtual bool DeepFrozen() const = 0;
		virtual bool Grounded() const = 0;
		virtual vec2 Position() const = 0;
		virtual vec2 Velocity() const = 0;
		virtual bool HammerDanger() const = 0;
		virtual bool RocketDanger(float Throw, float Spread) const = 0;
		virtual bool NearFreeze(float Clearance) const = 0;
		virtual bool WallStopped() const = 0;
		virtual bool HookedPlayer() const = 0;
		virtual float HazardRay(vec2 Position, vec2 Velocity) const = 0;
	};

	struct CProbeResult
	{
		int m_Life = 0;
		bool m_Died = false;
		float m_Speed = 0;
	};

	struct CSelection
	{
		CNetObj_PlayerInput m_Input{};
		bool m_Found = false;
		int m_Weapon = -1;
		int m_HookHold = 0;
		int m_Move = -1;
		bool m_Chained = false;
		bool m_StableEnd = false;
	};

	class CPlanner
	{
		std::vector<CMove> m_vMoves;
		std::vector<CEvaluation> m_vEvaluations;
		CMove m_LastMove;
		bool m_HasLast = false;
		bool m_LastHandBack = false;
		int m_LastWindow = 0;
		int m_LastSafeIndex = -1;

		int Chain(IWorld &World, const CNetObj_PlayerInput &Base, const CWeights &Weights, int Ticks);

	public:
		void Reset() { *this = {}; }
		void SetCandidates(std::vector<CMove> vMoves) { m_vMoves = std::move(vMoves); }
		const std::vector<CMove> &Candidates() const { return m_vMoves; }
		std::vector<CMove> &Candidates() { return m_vMoves; }
		const std::vector<CEvaluation> &Evaluations() const { return m_vEvaluations; }
		const CMove &LastMove() const { return m_LastMove; }
		int LastWindow() const { return m_LastWindow; }
		bool CanFollow(IWorld &World, const CNetObj_PlayerInput &Base, int Start, int Ticks, bool FreezeFatal) const;
		CNetObj_PlayerInput FollowInput(const CNetObj_PlayerInput &Base, int Step) const;
		bool HasEscape(IWorld &World, const CNetObj_PlayerInput &Base, const CWeights &Weights, int Ticks);
		CSelection Select(IWorld &World, const CNetObj_PlayerInput &Base, const CWeights &Weights, int Ticks);
	};

	// Pure policy, separate from DDNet's mutable input and world ownership.
	CNetObj_PlayerInput MoveInput(const CMove &Move, const CNetObj_PlayerInput &Base, int Step, bool HandBack);
	float InputDifference(const CNetObj_PlayerInput &Input, const CNetObj_PlayerInput &Reference, float JumpCost, float ReleaseHookCost);
	CProbeResult Probe(IWorld &World, const CNetObj_PlayerInput &Input, int Ticks, bool FreezeFatal);
	std::vector<CMove> MovementCandidates(CCollision *pCollision, vec2 Position, float HookLength, int HookState, bool CanJump, int HookAngles, bool AllowHook, std::vector<vec2> *pHookPoints = nullptr);
	void AddWeaponCandidates(std::vector<CMove> &vMoves, const std::vector<vec2> &vHookPoints, vec2 Position, bool Grenade, std::optional<vec2> HammerTarget, bool CanJump, int ActiveWeapon, int Reload);
	CEvaluation Evaluate(IWorld &World, const CMove &Move, const CNetObj_PlayerInput &Base, const CWeights &Weights, int Ticks, float CostLimit = 0);
}

#endif
