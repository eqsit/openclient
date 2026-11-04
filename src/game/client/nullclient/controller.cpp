#include "controller.h"

#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>

namespace OpenKrx::NullClient
{
	namespace
	{
		CNetObj_PlayerInput Cut(CNetObj_PlayerInput Input, unsigned Mask)
		{
			if(Mask & 1U)
				Input.m_Hook = 0;
			if(Mask & 2U)
				Input.m_Jump = 0;
			if(Mask & 4U)
				Input.m_Direction = 0;
			return Input;
		}
		int Horizon(const CSettings &Settings) { return Settings.m_Deep ? std::max(Settings.m_PlanHorizon, Settings.m_DeepHorizon) : Settings.m_PlanHorizon; }
		bool Safe(const CProbeResult &Result, int Ticks) { return Result.m_Life == Ticks; }
	}

	void CController::Generate(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Input, bool Weapons, int ActiveWeapon, bool Immediate, bool RocketTarget)
	{
		auto *pCharacter = World.Character();
		if(!pCharacter)
		{
			m_Planner.SetCandidates({});
			return;
		}
		const bool CanJump = World.Grounded() || !(pCharacter->Core()->m_Jumped & 2);
		const float HookLength = pCharacter->GetTuning(pCharacter->GetPureTuneZone())->m_HookLength;
		std::vector<vec2> vPoints;
		auto vMoves = MovementCandidates(World.World().Collision(), World.Position(), HookLength, pCharacter->Core()->m_HookState, CanJump, Settings.m_HookAngles, m_Output.m_HookTicks < 1, &vPoints);
		if(Weapons && Settings.m_Weapons)
		{
			// 2003d0 rejects all weapon candidates unless the caller supplied a ready
			// active weapon. Auto-switch candidates still use the current reload state.
			const bool Switch = Immediate && Settings.m_AutoWeapon;
			const bool Grenade = ActiveWeapon >= 0 && Settings.m_Grenade && (ActiveWeapon == WEAPON_GRENADE || (Switch && pCharacter->GetWeaponGot(WEAPON_GRENADE) && pCharacter->GetWeaponAmmo(WEAPON_GRENADE) != 0));
			const bool Hammer = ActiveWeapon >= 0 && Settings.m_Hammer && (ActiveWeapon == WEAPON_HAMMER || (Switch && pCharacter->GetWeaponGot(WEAPON_HAMMER) && pCharacter->GetWeaponAmmo(WEAPON_HAMMER) != 0));
			const auto Target = RocketTarget ? World.NearestRocketThreat() : World.NearestCharacter(120.0f);
			AddWeaponCandidates(vMoves, vPoints, World.Position(), Grenade, Hammer ? Target : std::nullopt, CanJump, ActiveWeapon, pCharacter->DiagnosticReloadTimer());
		}
		m_Planner.SetCandidates(std::move(vMoves));
	}

	bool CController::DeferredEscape(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Input, const CWeights &Weights, bool FreezeFatal, const CNetObj_PlayerInput *pFirst)
	{
		World.Reset();
		for(int Step = 0; Step < std::max(1, Settings.m_Grace); ++Step)
			if(!World.Tick(Step == 0 && pFirst ? *pFirst : Input) || World.Dead() || (FreezeFatal && World.Frozen()))
				return false;
		const vec2 Position = World.Position();
		World.CommitRoot();
		Generate(World, Settings, Input, false, -1, false);
		auto FutureWeights = Weights;
		FutureWeights.m_Start = Position;
		FutureWeights.m_Travel = 0;
		FutureWeights.m_FreezeFatal = FreezeFatal;
		FutureWeights.m_Chain = 0;
		return m_Planner.HasEscape(World, Input, FutureWeights, Horizon(Settings));
	}

	bool CController::CutSafe(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Input, unsigned Mask, bool FreezeFatal, int Window, bool AdvanceFirst)
	{
		World.Reset();
		if(AdvanceFirst && (!World.Tick(Input) || World.Dead()))
			return false;
		const auto Released = Cut(Input, Mask);
		if(!Safe(Probe(World, Released, Window, FreezeFatal), Window))
			return false;
		if(Settings.m_Coast > 0)
			return Safe(Probe(World, Cut(Released, 7), Settings.m_Coast, FreezeFatal), Settings.m_Coast);
		return true;
	}

	void CController::SetCut(const CNetObj_PlayerInput &Input, unsigned Mask, int Hold)
	{
		m_Output.m_Plan = Cut(Input, Mask);
		m_Output.m_CutTicks = Hold;
		m_Output.m_CutMask = Mask;
		m_Output.m_HasPlan = true;
		m_FollowCount = m_FollowStep = 0;
		m_pStage = "release";
		if(Mask & 1U)
			m_Output.m_HookTicks = 0;
	}

	bool CController::Veto(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Input, bool FreezeFatal)
	{
		m_Output.m_HasVeto = false;
		m_Output.m_aVetoDirections = {};
		m_Output.m_VetoJump = m_Output.m_VetoHook = false;
		// Original's veto_ticks=0 returns here, preserving the old fire flag but
		// leaving HasVeto false. It does not mean only movement veto is disabled.
		if(Settings.m_VetoTicks < 1)
			return false;
		for(int Direction = -1; Direction <= 1; ++Direction)
		{
			auto ProbeInput = Input;
			ProbeInput.m_Direction = Direction;
			World.Reset();
			m_Output.m_aVetoDirections[Direction + 1] = !Safe(Probe(World, ProbeInput, Settings.m_VetoTicks, FreezeFatal), Settings.m_VetoTicks);
		}
		const bool AnyDirection = m_Output.m_aVetoDirections[0] || m_Output.m_aVetoDirections[1] || m_Output.m_aVetoDirections[2];
		const bool SelectiveDirection = AnyDirection && !m_Output.m_aVetoDirections[1];
		if(!SelectiveDirection)
			m_Output.m_aVetoDirections = {};
		auto ProbeInput = Input;
		ProbeInput.m_Jump = 1;
		World.Reset();
		const int JumpWindow = std::max(Settings.m_VetoTicks, Settings.m_VetoHook);
		m_Output.m_VetoJump = !Safe(Probe(World, ProbeInput, JumpWindow, FreezeFatal), JumpWindow);
		if(!Input.m_Hook && Settings.m_VetoHook > 0)
		{
			ProbeInput = Input;
			ProbeInput.m_Hook = 1;
			World.Reset();
			m_Output.m_VetoHook = !Safe(Probe(World, ProbeInput, Settings.m_VetoHook, FreezeFatal), Settings.m_VetoHook);
			if(m_Output.m_VetoHook && Settings.m_VetoHook > 1)
			{
				World.Reset();
				bool Saved = World.Tick(ProbeInput) && !World.Dead() && (!FreezeFatal || !World.Frozen());
				if(Saved)
					Saved = Safe(Probe(World, Input, Settings.m_VetoHook - 1, FreezeFatal), Settings.m_VetoHook - 1);
				m_Output.m_VetoHook = !Saved;
			}
		}
		m_Output.m_VetoFire = false;
		World.Reset();
		if(Settings.m_VetoFire > 0)
		{
			auto *pCharacter = World.Character();
			if(pCharacter && pCharacter->GetActiveWeapon() == WEAPON_GRENADE && pCharacter->DiagnosticReloadTimer() == 0 && pCharacter->m_FreezeTime == 0 && pCharacter->GetWeaponAmmo(WEAPON_GRENADE) != 0)
			{
				ProbeInput = Input;
				ProbeInput.m_Fire = static_cast<int>(static_cast<unsigned>(Input.m_Fire) + 2U);
				m_Output.m_VetoFire = !Safe(Probe(World, ProbeInput, Settings.m_VetoFire, FreezeFatal), Settings.m_VetoFire);
			}
		}
		m_Output.m_HasVeto = SelectiveDirection || m_Output.m_VetoJump || m_Output.m_VetoHook || m_Output.m_VetoFire;
		return AnyDirection || m_Output.m_VetoJump || m_Output.m_VetoHook || m_Output.m_VetoFire;
	}

	bool CController::Defensive(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Raw, const CNetObj_PlayerInput &Input, const CWeights &Weights, bool FreezeFatal, bool Idle, bool AllowWeapon)
	{
		World.Reset();
		auto *pCharacter = World.Character();
		if(!pCharacter)
			return false;
		const int Weapon = pCharacter->GetActiveWeapon();
		Generate(World, Settings, Input, AllowWeapon, Weapon, false, true);
		const auto Selection = m_Planner.Select(World, Input, Weights, Horizon(Settings));
		if(!Selection.m_Found || !Selection.m_StableEnd)
			return false;
		World.Reset();
		int Life = 0;
		if(World.Tick(Selection.m_Input) && !World.Dead() && (!FreezeFatal || !World.Frozen()))
			Life = Probe(World, Input, Settings.m_Horizon - 1, FreezeFatal).m_Life + 1;
		if(Life < m_BaselineLife)
			return false;
		if(Life < Settings.m_Horizon)
		{
			// DeferredEscape resets to the original root, so use another root clone,
			// not the already-advanced life probe above.
			World.Reset();
			auto pRoot = World.Clone();
			if(!DeferredEscape(*static_cast<CSimulation *>(pRoot.get()), Settings, Input, m_LastWeights, FreezeFatal, &Selection.m_Input))
				return false;
		}
		m_Output.m_Plan = Selection.m_Input;
		m_Output.m_HasPlan = true;
		m_Active = false;
		m_FollowCount = m_FollowStep = 0;
		m_Output.m_HookTicks = Selection.m_HookHold;
		m_DefensiveHold = Selection.m_HookHold > 0;
		if(Selection.m_HookHold > 0)
		{
			m_Output.m_HookX = Selection.m_Input.m_TargetX;
			m_Output.m_HookY = Selection.m_Input.m_TargetY;
		}
		// Original defensive pass writes the proposed weapon fields into the plan
		// but does not set the one-shot action; Apply deliberately preserves live
		// fire/weapon fields until the main emergency search issues an action.
		m_pStage = "defensive";
		return true;
	}

	bool CController::LaserRescue(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Input, int ReadyWeapon)
	{
		if(!Settings.m_Weapons || !Settings.m_Laser || ReadyWeapon != WEAPON_LASER)
			return false;
		World.Reset();
		auto *pCharacter = World.Character();
		if(!pCharacter || !pCharacter->GetWeaponGot(WEAPON_LASER) || pCharacter->GetWeaponAmmo(WEAPON_LASER) == 0 || pCharacter->DiagnosticReloadTimer() != 0)
			return false;
		const vec2 Position = World.Position();
		const float Reach = pCharacter->GetTuning(pCharacter->GetPureTuneZone())->m_LaserReach;
		for(int Index = 0; Index < 48; ++Index)
		{
			const float Angle = static_cast<float>(Index) * 6.2831855f / 48.0f;
			vec2 Hit;
			if(!World.World().Collision()->IntersectLine(Position, Position + vec2(std::cos(Angle), std::sin(Angle)) * Reach, &Hit, nullptr) || distance(Position, Hit) < 24.0f)
				continue;
			const vec2 Delta = Hit - Position;
			const int X = static_cast<int>(Delta.x), Y = static_cast<int>(Delta.y);
			if(!X && !Y)
				continue;
			auto Shot = Input;
			Shot.m_TargetX = X;
			Shot.m_TargetY = Y;
			Shot.m_WantedWeapon = WEAPON_LASER + 1;
			Shot.m_Fire = static_cast<int>(static_cast<unsigned>(Input.m_Fire) + 1U);
			World.Reset();
			if(!World.Tick(Shot) || World.Dead())
				continue;
			bool WasFrozen = World.Frozen(), Alive = true;
			CNetObj_PlayerInput Coast{};
			Coast.m_TargetX = X;
			Coast.m_TargetY = Y;
			Coast.m_WantedWeapon = WEAPON_LASER + 1;
			Coast.m_Fire = Shot.m_Fire;
			for(int Step = 1; Step < Horizon(Settings); ++Step)
			{
				if(!World.Tick(Coast) || World.Dead())
				{
					Alive = false;
					break;
				}
				WasFrozen |= World.Frozen();
			}
			if(Alive && WasFrozen && !World.Frozen())
			{
				m_Output.m_Plan = Input;
				m_Output.m_Plan.m_TargetX = X;
				m_Output.m_Plan.m_TargetY = Y;
				m_Output.m_Plan.m_WantedWeapon = WEAPON_LASER + 1;
				m_Output.m_WeaponAction = WEAPON_LASER;
				m_FollowCount = m_FollowStep = 0;
				m_pStage = "laser";
				return true;
			}
		}
		return false;
	}

	bool CController::PreserveManualJump(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Raw, const CFrame &Frame)
	{
		if(!Settings.m_Enabled || Frame.m_Dummy || !Raw.m_Jump)
			return false;
		World.Reset();
		auto *pCharacter = World.Character();
		if(!pCharacter || World.Dead() || World.Frozen() ||
			(pCharacter->Core()->m_Jumped & 1) ||
			(!World.Grounded() && (pCharacter->Core()->m_Jumped & 2)))
			return false;
		// Inspect emission without consuming the controller's one-shot weapon action.
		auto Preview = m_Output;
		if(Preview.Apply(Raw, true, true, true, pCharacter->GetActiveWeapon(), Settings).m_Jump)
			return false;
		const int ActiveWeapon = Frame.m_ResolvePendingWeapon ? WeaponOnDirectInput(World.World(), pCharacter->GetCid(), Raw) : pCharacter->GetActiveWeapon();
		const int ReadyWeapon = pCharacter->DiagnosticReloadTimer() == 0 && pCharacter->GetWeaponAmmo(ActiveWeapon) != 0 ? ActiveWeapon : -1;
		const bool UseHookAim = pCharacter->Core()->m_HookState != HOOK_GRABBED;
		// Reuse Null's real hook/weapon moves and scorer, constrained to jump on
		// the first tick. A failed search leaves the original decision intact.
		auto SavedPlanner = std::move(m_Planner);
		Generate(World, Settings, Raw, true, ReadyWeapon, true);
		auto JumpPlanner = std::move(m_Planner);
		m_Planner = std::move(SavedPlanner);
		auto &Moves = JumpPlanner.Candidates();
		Moves.erase(std::remove_if(Moves.begin(), Moves.end(), [](const CMove &Move) { return Move.m_Jump == 0; }), Moves.end());
		CWeights Weights;
		Weights.m_Anchor = m_Anchor;
		Weights.m_Start = World.Position();
		Weights.m_Hold = static_cast<float>(Settings.m_Hold) / 100.0f;
		Weights.m_HoldSlack = Settings.m_HoldSlack;
		Weights.m_Settle = Settings.m_Settle;
		Weights.m_Travel = static_cast<float>(Settings.m_Travel) / 100.0f;
		Weights.m_pManual = &Raw;
		Weights.m_Deviation = Settings.m_Deviation;
		Weights.m_FreezeCost = std::max(100, Settings.m_FreezeCost);
		Weights.m_Commitment = Settings.m_Commitment;
		Weights.m_HookCost = Settings.m_HookCost;
		Weights.m_AirJump = Settings.m_AirJump;
		Weights.m_HookAge = m_HookAge;
		Weights.m_HookAgeCost = Settings.m_HookAge;
		Weights.m_WallCost = Settings.m_WallCost;
		Weights.m_TeeHookCost = Settings.m_TeeHookCost;
		Weights.m_HammerCost = Settings.m_HammerDodge ? Settings.m_HammerCost : 0;
		Weights.m_RocketCost = World.RocketThreatCount() > 0 ? Settings.m_RocketCost : 0;
		Weights.m_RocketThrow = Settings.m_RocketThrow;
		Weights.m_RocketSpread = Settings.m_RocketSpread;
		Weights.m_Coast = Settings.m_Coast;
		Weights.m_HazardSpeed = static_cast<float>(Settings.m_HazardSpeed) / 10.0f;
		// No chained prefix: this extension must have a concrete validated plan,
		// including its weapon switch/reload and coast, before emitting the jump.
		const int Window = std::max(Settings.m_Horizon, Horizon(Settings));
		const auto Selection = JumpPlanner.Select(World, Raw, Weights, Window);
		if(!Selection.m_Found || !Selection.m_StableEnd || !Selection.m_Input.m_Jump)
			return false;
		m_Planner = std::move(JumpPlanner);
		m_Output.m_Plan = Selection.m_Input;
		m_Output.m_HasPlan = true;
		m_Output.m_HasVeto = false;
		m_Output.m_CutTicks = 0;
		m_Output.m_HookTicks = Selection.m_HookHold;
		m_Output.m_UseHookAim = UseHookAim;
		m_Output.m_HookX = Selection.m_Input.m_TargetX;
		m_Output.m_HookY = Selection.m_Input.m_TargetY;
		m_Output.m_WeaponAction = Selection.m_Weapon;
		m_FollowCount = std::min(m_Planner.LastWindow(), Settings.m_Follow);
		m_FollowStep = m_FollowCount > 0 ? 1 : 0;
		if(Selection.m_Weapon >= 0 && Selection.m_Weapon != ReadyWeapon)
			m_FollowCount = m_FollowStep = 0;
		m_DefensiveHold = false;
		m_Previous = m_Output.m_Plan;
		m_HadPrevious = m_Active = true;
		m_pStage = "manual jump rescue";
		return true;
	}

	void CController::Decide(CSimulation &World, const CSettings &Settings, const CNetObj_PlayerInput &Raw, const CFrame &Frame)
	{
		if(m_LastTick == Frame.m_Tick)
			return;
		m_LastTick = Frame.m_Tick;
		const bool WasActive = m_Active;
		m_Output.m_HasPlan = false;
		m_Output.m_HasVeto = false;
		m_Output.m_WeaponAction = -1;
		m_pStage = "watching";
		if(m_Output.m_HookTicks > 0)
			--m_Output.m_HookTicks;
		if(m_Output.m_CutTicks > 0)
			--m_Output.m_CutTicks;
		if(!(Frame.m_Dummy ? Settings.m_Dummy : Settings.m_Enabled))
		{
			m_Active = false;
			m_Output.m_HookTicks = m_Output.m_CutTicks = 0;
			m_HadPrevious = false;
			m_pStage = "disabled";
			return;
		}
		World.Reset();
		if(!World.Valid())
		{
			m_Active = false;
			m_HadPrevious = false;
			m_pStage = "missing";
			return;
		}
		const vec2 Position = World.Position();
		if(!m_AnchorValid)
		{
			m_Anchor = Position;
			m_AnchorValid = true;
		}
		if(World.Frozen())
		{
			m_Active = false;
			m_Anchor = Position;
			m_HadPrevious = false;
			m_pStage = "frozen";
			return;
		}
		auto *pCharacter = World.Character();
		if(m_Output.m_HookTicks > 0)
		{
			const int Hook = pCharacter->Core()->m_HookState;
			if(Hook != HOOK_FLYING && Hook != HOOK_GRABBED)
				m_Output.m_HookTicks = 0;
			m_Output.m_UseHookAim = Hook != HOOK_GRABBED;
		}
		auto Input = Raw;
		if(m_Output.m_HookTicks > 0 && !Raw.m_Hook)
		{
			Input.m_Hook = 1;
			Input.m_TargetX = m_Output.m_HookX;
			Input.m_TargetY = m_Output.m_HookY;
		}
		else
			m_Output.m_HookTicks = 0;
		if(m_Output.m_CutTicks > 0)
		{
			const unsigned Held = (Raw.m_Direction ? 4U : 0U) | (Raw.m_Jump ? 2U : 0U) | (Raw.m_Hook ? 1U : 0U);
			if(!(m_Output.m_CutMask & Held))
				m_Output.m_CutTicks = 0;
			else
				Input = Cut(Input, m_Output.m_CutMask);
		}
		m_HookAge = ((m_HadPrevious && m_Previous.m_Hook) || m_Output.m_HookTicks > 0) && !Raw.m_Hook ? m_HookAge + 1 : 0;
		const vec2 Velocity = World.Velocity();
		const float Shock = m_VelocityTick == Frame.m_Tick - 1 ? length(Velocity - m_PreviousVelocity) : 0;
		const float PreviousSpeed = length(m_PreviousVelocity), Speed = length(Velocity);
		m_PreviousVelocity = Velocity;
		m_VelocityTick = Frame.m_Tick;
		const int Jump = pCharacter->Core()->m_Jumped;
		const bool JumpChanged = m_JumpTick == Frame.m_Tick - 1 && Jump != m_LastJump;
		m_LastJump = Jump;
		m_JumpTick = Frame.m_Tick;
		if(World.HasOwnedProjectile())
			m_LastOwnProjectile = Frame.m_Tick;
		const bool RecentAttack = Frame.m_AuthoritativeWeapon >= WEAPON_SHOTGUN && Frame.m_AuthoritativeWeapon <= WEAPON_LASER && static_cast<unsigned>(Frame.m_Tick - Frame.m_AuthoritativeAttackTick) <= 20U;
		const bool OwnImpulse = Frame.m_Tick - m_LastOwnProjectile < 2 || RecentAttack;
		const bool SelfAcceleration = OwnImpulse || pCharacter->Core()->m_HookState == HOOK_FLYING || pCharacter->Core()->m_HookState == HOOK_GRABBED || JumpChanged;
		vec2 HookerPosition;
		const int Hooker = World.IncomingHooker(&HookerPosition);
		if(Hooker != m_CounterHooker)
			m_CounterLocked = false;
		m_CounterHooker = Hooker;
		const bool ShockKnockback = Shock >= 6.0f && Speed > PreviousSpeed && !SelfAcceleration;
		const bool UnderPull = Hooker >= 0 || ShockKnockback;
		if(OwnImpulse)
			m_LastKnockback = -1000;
		if(Settings.m_Knockback && (Hooker >= 0 || ShockKnockback))
			m_LastKnockback = Frame.m_Tick;
		const bool KnockbackHold = Settings.m_Knockback && Frame.m_Tick - m_LastKnockback <= Settings.m_KnockbackHold;
		const bool Idle = !Raw.m_Direction && !Raw.m_Jump && !Raw.m_Hook;
		const int ActiveWeapon = Frame.m_ResolvePendingWeapon ? WeaponOnDirectInput(World.World(), pCharacter->GetCid(), Input) : pCharacter->GetActiveWeapon();
		const int ReadyWeapon = pCharacter->DiagnosticReloadTimer() == 0 && pCharacter->m_FreezeTime == 0 && pCharacter->GetWeaponAmmo(ActiveWeapon) != 0 ? ActiveWeapon : -1;
		const bool FreezeCost = Settings.m_FreezeCost > 0 || Frame.m_Dummy;
		const bool FreezeFatal = FreezeCost && Frame.m_FreezeTerminal;
		World.Reset();
		const auto Baseline = Probe(World, Input, Settings.m_Horizon, FreezeCost);
		m_BaselineLife = Baseline.m_Life;
		m_BaselineDied = Baseline.m_Died;
		int Reaction = Settings.m_React + (Hooker >= 0 ? Settings.m_ReactHooked : 0);
		Reaction += static_cast<int>(static_cast<float>(Settings.m_ReactShock) * Shock / 100.0f) - static_cast<int>(Speed * Speed * static_cast<float>(Settings.m_ReactSpeedFast) / -10000.0f) - static_cast<int>(static_cast<float>(Settings.m_ReactSpeed) * Speed / -100.0f);
		if(Hooker >= 0 && (World.RocketThreatCount() > 0 || World.HammerThreatCount() > 0))
			Reaction += Settings.m_Horizon;
		m_Reaction = std::clamp(Reaction, 1, Settings.m_Horizon);
		World.Reset();
		// First let-go branch: if releasing now survives but releasing after one
		// additional held-input tick does not, emit the release on this tick.
		if(Settings.m_LetGo && Input.m_Hook && m_BaselineLife < Settings.m_Horizon)
		{
			const auto Released = Cut(Input, 1);
			const bool ReleaseSafe = Safe(Probe(World, Released, Settings.m_Horizon, FreezeCost), Settings.m_Horizon);
			const bool CoastSafe = ReleaseSafe && (Settings.m_Coast <= 0 || Safe(Probe(World, Cut(Released, 7), Settings.m_Coast, FreezeCost), Settings.m_Coast));
			if(CoastSafe)
			{
				World.Reset();
				const bool FirstSafe = World.Tick(Input) && !World.Dead();
				if(!FirstSafe || !Safe(Probe(World, Released, Settings.m_Horizon, FreezeCost), Settings.m_Horizon))
				{
					m_Active = false;
					m_Output.m_HasVeto = false;
					SetCut(Input, 1, Settings.m_CutHold);
					m_HadPrevious = true;
					m_Previous = m_Output.m_Plan;
					return;
				}
			}
		}
		World.Reset();
		pCharacter = World.Character();
		CWeights Weights;
		Weights.m_Anchor = m_Anchor;
		Weights.m_Start = Position;
		Weights.m_Hold = static_cast<float>(Settings.m_Hold) / 100.0f;
		Weights.m_HoldSlack = Settings.m_HoldSlack;
		Weights.m_Settle = Settings.m_Settle;
		Weights.m_Travel = static_cast<float>(Settings.m_Travel) / 100.0f;
		Weights.m_HandBack = Settings.m_HandBack != 0;
		Weights.m_pManual = &Raw;
		Weights.m_Deviation = Settings.m_Deviation;
		if(WasActive && m_HadPrevious)
		{
			Weights.m_pPrevious = &m_Previous;
			Weights.m_Continuity = Settings.m_Continuity;
		}
		Weights.m_FreezeCost = Frame.m_Dummy ? std::max(100, Settings.m_FreezeCost) : Settings.m_FreezeCost;
		Weights.m_HammerCost = Settings.m_HammerDodge ? Settings.m_HammerCost : 0;
		Weights.m_RocketCost = World.RocketThreatCount() > 0 ? Settings.m_RocketCost : 0;
		Weights.m_RocketThrow = Settings.m_RocketThrow;
		Weights.m_RocketSpread = Settings.m_RocketSpread;
		Weights.m_AirJump = Settings.m_AirJump;
		Weights.m_Commitment = Settings.m_Commitment;
		Weights.m_HookCost = Settings.m_HookCost;
		Weights.m_HookAge = m_HookAge;
		Weights.m_HookAgeCost = Settings.m_HookAge;
		Weights.m_WallCost = Settings.m_WallCost;
		Weights.m_TeeHookCost = Settings.m_TeeHookCost;
		Weights.m_Chain = Settings.m_Deep && Settings.m_Chain > 0 ? std::max(Settings.m_Chain, Settings.m_DeepChain) : Settings.m_Chain;
		Weights.m_Coast = Settings.m_Coast;
		Weights.m_HazardSpeed = static_cast<float>(Settings.m_HazardSpeed) / 10.0f;
		m_LastWeights = Weights;
		if(UnderPull)
			Weights.m_Hold *= static_cast<float>(Settings.m_Antiknockback) / 100.0f;
		if(Input.m_Hook && pCharacter->Core()->HookedPlayer() >= 0)
			Weights.m_ReleaseHookCost = 3;
		if(Settings.m_HookCounter && Hooker >= 0)
		{
			if(!m_CounterLocked)
			{
				m_CounterLocked = true;
				m_CounterBelow = HookerPosition.y < Position.y;
			}
			Weights.m_CounterY = HookerPosition.y;
			Weights.m_CounterBelow = m_CounterBelow;
			Weights.m_CounterPull = static_cast<float>(Settings.m_HookCounterPull) / 100.0f;
			Weights.m_Anchor = vec2(HookerPosition.x, HookerPosition.y + (m_CounterBelow ? Settings.m_HookCounterGap : -Settings.m_HookCounterGap));
			Weights.m_HoldSlack = Weights.m_Settle = Weights.m_Travel = Weights.m_Deviation = Weights.m_HammerCost = Weights.m_HookCost = 0;
			Weights.m_Hold = std::max(Weights.m_Hold, Weights.m_CounterPull);
		}
		int Threshold = m_Reaction;
		if(WasActive)
			Threshold = std::min(Settings.m_Horizon, Threshold + Settings.m_Hysteresis);
		if(m_FollowStep < m_FollowCount && m_BaselineLife < Threshold && m_Planner.CanFollow(World, Input, m_FollowStep, Horizon(Settings), FreezeCost))
		{
			m_Output.m_Plan = m_Planner.FollowInput(Input, m_FollowStep++);
			m_Output.m_HasPlan = true;
			m_Active = true;
			m_pStage = "follow";
			m_Previous = m_Output.m_Plan;
			m_HadPrevious = true;
			return;
		}
		m_FollowCount = m_FollowStep = 0;
		const auto TryDefensive = [&]() {
			World.Reset();
			// These passes have distinct scoring policies and run in this order.
			if(Settings.m_RocketDodge && Settings.m_RocketCost > 0 && World.RocketThreatCount() > 0)
			{
				int Danger = World.RocketDanger(Settings.m_RocketThrow, Settings.m_RocketSpread) ? 0 : World.RocketApproach(Settings.m_RocketLookahead);
				if(Danger != 0)
				{
					for(int Step = 1; Step <= Settings.m_RocketLookahead; ++Step)
					{
						if(!World.Tick(Input) || World.Dead())
							break;
						if(World.RocketDanger(Settings.m_RocketThrow, Settings.m_RocketSpread))
						{
							if(Danger < 0 || Step < Danger)
								Danger = Step;
							break;
						}
					}
				}
				if(Danger >= 0)
				{
					auto W = Weights;
					W.m_Clearance = Settings.m_KnockbackClearance;
					W.m_HammerCost = Settings.m_HammerDodge ? Settings.m_HammerCost : 0;
					W.m_RocketCost = Settings.m_RocketCost;
					W.m_Travel = 0;
					W.m_RespectHeld = !Idle;
					W.m_FreezeFatal = FreezeCost;
					if(Defensive(World, Settings, Raw, Input, W, FreezeCost, Idle, Settings.m_RocketAnswer != 0))
						return true;
				}
			}
			World.Reset();
			if(Settings.m_Knockback && (Idle || Settings.m_KnockbackMode) && KnockbackHold)
			{
				const float Displacement = distance(World.Position(), m_Anchor);
				const float Away = Displacement > 0.001f ? dot((World.Position() - m_Anchor) / Displacement, Velocity) : Speed;
				if(Displacement > Settings.m_HoldSlack || Away >= 2.0f)
				{
					auto W = Weights;
					W.m_Hold = static_cast<float>(Settings.m_KnockbackGrip) / 100.0f;
					W.m_Travel = 0;
					W.m_Clearance = Settings.m_KnockbackClearance;
					W.m_RespectHeld = !Idle;
					W.m_FreezeFatal = FreezeCost;
					if(Idle)
						W.m_HookCost = 100000.0f;
					if(Defensive(World, Settings, Raw, Input, W, FreezeCost, Idle, false))
						return true;
				}
				else
					m_LastKnockback = -1000;
			}
			World.Reset();
			if(Settings.m_HammerDodge && Settings.m_HammerCost > 0 && World.HammerThreatCount() > 0)
			{
				bool Danger = World.HammerDanger();
				for(int Step = 0; !Danger && Step < Settings.m_HammerLookahead; ++Step)
				{
					if(!World.Tick(Input) || World.Dead())
						break;
					Danger = World.HammerDanger();
				}
				if(Danger)
				{
					auto W = Weights;
					W.m_HammerCost = Settings.m_HammerCost;
					W.m_Travel = 0;
					W.m_RespectHeld = !Idle;
					W.m_FreezeFatal = FreezeCost;
					if(Defensive(World, Settings, Raw, Input, W, FreezeCost, Idle, false))
						return true;
				}
			}
			return false;
		};
		if(m_BaselineLife >= Threshold)
		{
			m_Active = false;
			m_pStage = "calm";
			const bool AnyVeto = Veto(World, Settings, Input, FreezeCost);
			if(!AnyVeto && !UnderPull && !KnockbackHold)
				m_Anchor = Position;
			TryDefensive();
			m_HadPrevious = m_Output.m_HasPlan;
			if(m_HadPrevious)
				m_Previous = m_Output.m_Plan;
			return;
		}
		m_Active = true;
		if(m_DefensiveHold)
		{
			m_Output.m_HookTicks = 0;
			m_DefensiveHold = false;
		}
		Veto(World, Settings, Input, FreezeCost);
		if(!UnderPull)
		{
			int Direction = Raw.m_Direction;
			if(Frame.m_Dummy)
			{
				const int Window = std::clamp(Settings.m_Horizon, 12, 60);
				int BestLife;
				auto ProbeInput = Input;
				ProbeInput.m_Direction = 0;
				World.Reset();
				BestLife = Probe(World, ProbeInput, Window, FreezeCost).m_Life;
				Direction = 0;
				if(BestLife < Window)
				{
					ProbeInput.m_Direction = -1;
					World.Reset();
					const int Left = Probe(World, ProbeInput, Window, FreezeCost).m_Life;
					ProbeInput.m_Direction = 1;
					World.Reset();
					const int Right = Probe(World, ProbeInput, Window, FreezeCost).m_Life;
					if(BestLife < Right && Left <= Right)
						Direction = 1;
					else if(BestLife < Left && Right < Left)
						Direction = -1;
				}
			}
			World.Reset();
			if(Direction)
			{
				const vec2 Candidate = m_Anchor + vec2((static_cast<float>(Settings.m_AnchorSpeed) / 50.0f) * static_cast<float>(Direction), 0);
				if(!World.World().Collision()->CheckPoint(round_to_int(Candidate.x), round_to_int(Candidate.y)))
					m_Anchor = Candidate;
				const float Delta = m_Anchor.x - Position.x;
				if(std::abs(Delta) > Settings.m_AnchorLeash)
					m_Anchor.x = Position.x + (Delta > 0 ? Settings.m_AnchorLeash : -Settings.m_AnchorLeash);
			}
			if((World.Grounded() && m_Anchor.y < Position.y) || Raw.m_Jump || Raw.m_Hook)
				m_Anchor.y = Position.y;
		}
		Weights.m_Anchor = m_Anchor;
		const bool Immediate = m_Reaction >= m_BaselineLife * 2 || UnderPull || (Settings.m_Weapons && World.HasProjectiles());
		if(Settings.m_LastMoment && m_BaselineLife > 0)
		{
			World.Reset();
			auto pFuture = World.Clone();
			if(DeferredEscape(*static_cast<CSimulation *>(pFuture.get()), Settings, Input, Weights, FreezeCost))
			{
				m_Active = false;
				m_pStage = "defer rescue";
				if(KnockbackHold || World.HammerThreatCount() > 0 || World.RocketThreatCount() > 0)
					if(TryDefensive())
					{
						m_Previous = m_Output.m_Plan;
						m_HadPrevious = true;
						return;
					}
				if(!UnderPull && !KnockbackHold)
					m_Anchor = Position;
				m_HadPrevious = false;
				return;
			}
		}
		World.Reset();
		if(Settings.m_LetGo)
		{
			const unsigned Held = (Input.m_Direction ? 4U : 0U) | (Input.m_Jump ? 2U : 0U) | (Input.m_Hook ? 1U : 0U);
			const int Window = Settings.m_CutWindow > 0 ? Settings.m_CutWindow : Horizon(Settings);
			for(unsigned Mask : {1U, 2U, 4U, 3U, 5U, 6U, 7U})
				if((Mask & Held) == Mask && CutSafe(World, Settings, Input, Mask, FreezeCost, Window, false))
				{
					SetCut(Input, Mask, Settings.m_CutHold);
					m_Previous = m_Output.m_Plan;
					m_HadPrevious = true;
					return;
				}
		}
		World.Reset();
		Generate(World, Settings, Input, true, ReadyWeapon, Immediate);
		auto Selection = m_Planner.Select(World, Input, Weights, Horizon(Settings));
		if(!Selection.m_Found)
		{
			bool HadWeapon = false;
			for(const auto &Move : m_Planner.Candidates())
				HadWeapon |= Move.m_WeaponAction >= 0;
			if(!HadWeapon)
			{
				World.Reset();
				Generate(World, Settings, Input, true, ReadyWeapon, true);
				for(const auto &Move : m_Planner.Candidates())
					HadWeapon |= Move.m_WeaponAction >= 0;
				if(HadWeapon)
					Selection = m_Planner.Select(World, Input, Weights, Horizon(Settings));
			}
		}
		m_pStage = Selection.m_Found ? "rescue" : "best effort";
		if(!Selection.m_Found && LaserRescue(World, Settings, Input, ReadyWeapon))
		{
			Selection.m_Weapon = WEAPON_LASER;
			Selection.m_HookHold = 0;
		}
		else if(Selection.m_Move >= 0)
			m_Output.m_Plan = Selection.m_Input;
		else
		{
			m_HadPrevious = false;
			return;
		}
		m_Output.m_HookTicks = Selection.m_HookHold;
		if(Selection.m_HookHold > 0)
		{
			m_Output.m_HookX = m_Output.m_Plan.m_TargetX;
			m_Output.m_HookY = m_Output.m_Plan.m_TargetY;
		}
		m_FollowCount = std::min(m_Planner.LastWindow(), Settings.m_Follow);
		m_FollowStep = m_FollowCount > 0 ? 1 : 0;
		if(Selection.m_Weapon >= 0)
		{
			m_Output.m_WeaponAction = Selection.m_Weapon;
			if(Selection.m_Weapon != ReadyWeapon)
				m_FollowCount = m_FollowStep = 0;
		}
		m_Output.m_HasPlan = true;
		m_Previous = m_Output.m_Plan;
		m_HadPrevious = true;
		(void)FreezeFatal;
	}
}
