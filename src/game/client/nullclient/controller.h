#ifndef GAME_CLIENT_NULLCLIENT_CONTROLLER_H
#define GAME_CLIENT_NULLCLIENT_CONTROLLER_H

#include "output.h"
#include "policy.h"
#include "simulation.h"

namespace OpenKrx::NullClient
{
	struct CFrame
	{
		int m_Tick = -1;
		bool m_Dummy = false;
		bool m_FreezeTerminal = true;
		bool m_ResolvePendingWeapon = false; // OpenKRX dispatch adaptation, not original oracle.
		int m_AuthoritativeWeapon = -1;
		int m_AuthoritativeAttackTick = -1000;
	};

	// Recovered 14020b540 decision state. Application to current input belongs to
	// COutputState, and the 14020a040 wrapper owns once-per-prediction-tick cadence.
	class CController
	{
		CPlanner m_Planner;
		COutputState m_Output;
		CNetObj_PlayerInput m_Previous{};
		bool m_HadPrevious = false;
		bool m_Active = false;
		bool m_AnchorValid = false;
		vec2 m_Anchor = vec2(0, 0);
		vec2 m_PreviousVelocity = vec2(0, 0);
		int m_VelocityTick = -1;
		int m_LastOwnProjectile = -1000;
		int m_LastKnockback = -1000;
		int m_LastJump = 0, m_JumpTick = -1;
		int m_HookAge = 0;
		int m_FollowStep = 0, m_FollowCount = 0;
		int m_CounterHooker = -1;
		bool m_CounterLocked = false, m_CounterBelow = false;
		bool m_DefensiveHold = false;
		int m_LastTick = -1;
		int m_BaselineLife = -1, m_Reaction = -1;
		bool m_BaselineDied = false;
		CWeights m_LastWeights;
		const char *m_pStage = "idle";

		void Generate(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Input, bool Weapons, int ActiveWeapon, bool Immediate, bool RocketTarget = false);
		bool DeferredEscape(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Input, const CWeights &Weights, bool FreezeFatal, const CNetObj_PlayerInput *pFirst = nullptr);
		bool CutSafe(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Input, unsigned Mask, bool FreezeFatal, int Window, bool AdvanceFirst);
		bool Veto(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Input, bool FreezeFatal);
		void SetCut(const CNetObj_PlayerInput &Input, unsigned Mask, int Hold);
		bool Defensive(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Raw, const CNetObj_PlayerInput &Input, const CWeights &Weights, bool FreezeFatal, bool Idle, bool AllowWeapon);
		bool LaserRescue(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Input, int ReadyWeapon);

	public:
		void Reset() { *this = {}; }
		void Decide(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Raw, const CFrame &Frame);
		// Optional OpenKRX manual-input extension; never called by original-policy fixtures.
		bool PreserveManualJump(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Raw, const CFrame &Frame);
		COutputState &Output() { return m_Output; }
		const COutputState &Output() const { return m_Output; }
		const char *Stage() const { return m_pStage; }
		int BaselineLife() const { return m_BaselineLife; }
		int Reaction() const { return m_Reaction; }
		bool Active() const { return m_Active; }
		int LastTick() const { return m_LastTick; }
		const CPlanner &Planner() const { return m_Planner; }
	};
}

#endif
