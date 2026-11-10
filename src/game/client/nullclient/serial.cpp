#include "serial.h"

#include "serial_threats.h"
#include "simulation.h"

#include <engine/shared/client_profile.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenKrx::NullClient
{
	namespace
	{
		bool Hazard(CCollision *pCollision, vec2 P)
		{
			const int Index = pCollision->GetPureMapIndex(P);
			const int A = pCollision->GetTileIndex(Index), B = pCollision->GetFrontTileIndex(Index);
			return A == TILE_FREEZE || B == TILE_FREEZE || A == TILE_DFREEZE || B == TILE_DFREEZE || A == TILE_DEATH || B == TILE_DEATH;
		}
		bool Frozen(const CCharacter *p) { return p && (p->m_FreezeTime > 0 || p->Core()->m_DeepFrozen); }
		vec2 Unit(vec2 V) { return length(V) > .001f ? normalize(V) : vec2(1, 0); }
		bool Lasting(CCollision *pCollision, vec2 P)
		{
			const int I = pCollision->GetPureMapIndex(P);
			if(pCollision->GetTileIndex(I) == TILE_DEATH || pCollision->GetFrontTileIndex(I) == TILE_DEATH)
				return true;
			for(int Step = 0; Step < 4; ++Step)
			{
				P.y += 32;
				if(pCollision->CheckPoint(P))
					return true;
				if(!Hazard(pCollision, P))
					return false;
			}
			return false;
		}
		std::optional<vec2> FreezeGoal(CCollision *pCollision, vec2 Target, float Range, bool LastingOnly)
		{
			std::optional<vec2> Best;
			float BestDistance = std::numeric_limits<float>::max();
			for(float Y = -Range; Y <= Range; Y += 32)
				for(float X = -Range; X <= Range; X += 32)
				{
					const vec2 P = Target + vec2(X, Y);
					const float D = distance(P, Target);
					if(D > Range || !Hazard(pCollision, P) || (LastingOnly && !Lasting(pCollision, P)))
						continue;
					int Surrounded = 0;
					for(const vec2 Offset : {vec2(32, 0), vec2(-32, 0), vec2(0, 32), vec2(0, -32)})
						Surrounded += Hazard(pCollision, P + Offset) || pCollision->CheckPoint(P + Offset);
					if(Surrounded < 4 && D < BestDistance)
					{
						Best = P;
						BestDistance = D;
					}
				}
			return Best;
		}
	}
	std::optional<vec2> CSerial::FindHookAnchor(CCollision *pCollision, vec2 Position, vec2 Delta, float HookLength)
	{
		// 23f660: sixteen anchors, hazard-safe visibility, upward preference,
		// and a small reach reward. The old 48-ray dot-only scan was a substitute.
		const float GoalLength = length(Delta);
		if(!pCollision || HookLength <= 0 || GoalLength < 24)
			return std::nullopt;
		const vec2 GoalDirection = Delta / GoalLength;
		std::optional<vec2> Best;
		float BestScore = 0;
		for(int Ray = 0; Ray < 16; ++Ray)
		{
			const float Angle = Ray * 6.2831855f * .0625f;
			const vec2 Direction(std::cos(Angle), std::sin(Angle));
			if(Direction.y > .4f && GoalDirection.y < .4f)
				continue;
			vec2 Hit;
			if(!pCollision->IntersectLineTeleHook(Position, Position + Direction * HookLength, &Hit, nullptr) ||
				!CRoute::SegmentSafe(pCollision, Position, Hit, true))
				continue;
			const vec2 Anchor = Hit - Position;
			const float Distance = length(Anchor);
			if(Distance < 48)
				continue;
			const vec2 UnitAnchor = Anchor / Distance;
			float Score = dot(GoalDirection, UnitAnchor);
			if(GoalDirection.y < -.2f)
				Score -= UnitAnchor.y * .8f;
			Score += std::clamp(Distance / HookLength, 0.f, 1.f) * .2f;
			if(!Best || Score > BestScore)
			{
				Best = Anchor;
				BestScore = Score;
			}
		}
		return BestScore > .2f ? Best : std::nullopt;
	}
	int CSerial::Pick(CGameClient *pClient, const CWarlist &Warlist, int Slot, int Mode) const
	{
		const int Id = pClient->m_aLocalIds[Slot];
		if(Mode == 4)
		{
			const int MainTarget = m_aStates[g_Config.m_ClDummy].m_Target;
			return MainTarget >= 0 && MainTarget < MAX_CLIENTS && MainTarget != Id && pClient->m_Snap.m_aCharacters[MainTarget].m_Active ? MainTarget : -1;
		}
		if(Id < 0 || Id >= MAX_CLIENTS)
			return -1;
		const vec2 Position = pClient->m_aClients[Id].m_RenderPos;
		const vec2 Mouse = Unit(pClient->m_Controls.m_aMousePos[Slot]);
		const int Group = Mode == 2 ? Warlist.Find("war") : Mode == 3 ? Warlist.Find("attack") :
										-1;
		int Best = -1;
		float BestScore = 0;
		for(int Other = 0; Other < MAX_CLIENTS; ++Other)
		{
			if(Other == Id || !pClient->m_Snap.m_aCharacters[Other].m_Active ||
				(!g_Config.m_NullSerialOwn && (Other == pClient->m_aLocalIds[0] || Other == pClient->m_aLocalIds[1])) ||
				(g_Config.m_NullSerialIgnoreFriends && pClient->m_aClients[Other].m_Friend))
				continue;
			if((Mode == 2 || Mode == 3) && !Warlist.Contains(Group, pClient->m_aClients[Other].m_aName))
				continue;
			const vec2 Delta = pClient->m_aClients[Other].m_RenderPos - Position;
			const float D = length(Delta);
			if(D < .001f || D > g_Config.m_NullSerialRange)
				continue;
			const float Score = Mode == 0 ? 1 - dot(Delta / D, Mouse) : D;
			if(Best < 0 || Score < BestScore)
			{
				Best = Other;
				BestScore = Score;
			}
		}
		return Best;
	}
	void CSerial::Lock(CGameClient *pClient, const CWarlist &Warlist)
	{
		auto &State = m_aStates[g_Config.m_ClDummy];
		const int Own = pClient->m_aLocalIds[g_Config.m_ClDummy];
		if(State.m_Id != Own)
		{
			State = {};
			State.m_Id = Own;
		}
		if(State.m_Locked)
		{
			State.m_Locked = false;
			pClient->Echo("serial: target released");
		}
		else
		{
			State.m_Target = Pick(pClient, Warlist, g_Config.m_ClDummy, 0);
			State.m_Locked = State.m_Target >= 0;
			pClient->Echo(State.m_Locked ? "serial: target locked" : "serial: nobody there");
		}
	}
	void CSerial::Update(CGameClient *pClient, const CWarlist &Warlist, const std::array<bool, MAX_CLIENTS> &Idle)
	{
		UpdateSlot(pClient, Warlist, g_Config.m_ClDummy, Idle);
		UpdateSlot(pClient, Warlist, !g_Config.m_ClDummy, Idle);
	}
	void CSerial::UpdateSlot(CGameClient *pClient, const CWarlist &Warlist, int Slot, const std::array<bool, MAX_CLIENTS> &Idle)
	{
		auto &S = m_aStates[Slot];
		const bool Main = Slot == g_Config.m_ClDummy;
		const int Id = pClient->m_aLocalIds[Slot];
		if(!NullProfileActive() || !(Main ? g_Config.m_NullSerialKiller : g_Config.m_NullSerialKillerDummy) ||
			Id < 0 || Id >= MAX_CLIENTS || !pClient->m_Snap.m_aCharacters[Id].m_Active || pClient->m_Snap.m_SpecInfo.m_Active)
		{
			S = {};
			return;
		}
		if(S.m_Id != Id)
		{
			S = {};
			S.m_Id = Id;
		}
		const int Tick = pClient->Client()->GameTick(Slot);
		if(S.m_LastTick == Tick)
			return;
		S.m_LastTick = Tick;
		S.m_HasPlan = false;
		auto &World = pClient->KrxPredictedWorld();
		auto *pSelf = World.GetCharacterById(Id);
		if(!pSelf || Frozen(pSelf) || pSelf->Core()->m_LiveFrozen)
			return;
		const int Mode = Main ? g_Config.m_NullSerialPick : g_Config.m_NullSerialPickDummy + 1;
		const int PreviousTarget = S.m_Target;
		if(!S.m_Locked || S.m_Target < 0 || S.m_Target >= MAX_CLIENTS || !pClient->m_Snap.m_aCharacters[S.m_Target].m_Active)
		{
			S.m_Locked = false;
			S.m_Target = Pick(pClient, Warlist, Slot, Mode);
		}
		const int TargetId = S.m_Target;
		if(TargetId < 0 || TargetId >= MAX_CLIENTS)
			return;
		if(PreviousTarget != S.m_Target)
		{
			S.m_Stuck = 0;
			S.m_HasHazard = false;
			S.m_HaveStand = false;
			S.m_Route.Reset();
			S.m_HaveVelocity = false;
		}
		auto *pTarget = World.GetCharacterById(TargetId);
		// 2417e0 uses rendered positions for chase geometry and predicted cores
		// for weapon/hook/velocity observations. Mixing these changes near-range
		// thresholds and can release a target at a different point in its throw.
		const vec2 Position = pClient->m_aClients[Id].m_RenderPos;
		const vec2 Target = pClient->m_aClients[TargetId].m_RenderPos;
		const float D = distance(Position, Target);
		if(D < .001f)
			return;
		const bool TargetFrozen = Frozen(pTarget);
		const vec2 ObservedVelocity = pTarget ? pTarget->Core()->m_Vel : vec2(pClient->m_Snap.m_aCharacters[TargetId].m_Cur.m_VelX, pClient->m_Snap.m_aCharacters[TargetId].m_Cur.m_VelY) / 256.f;
		S.m_Velocity = S.m_HaveVelocity ? S.m_Velocity * .7f + ObservedVelocity * .3f : ObservedVelocity;
		S.m_HaveVelocity = true;
		auto *pCollision = pClient->Collision();
		const bool Hooked = pSelf->Core()->HookedPlayer() == S.m_Target;
		if(TargetFrozen && Hazard(pCollision, Target) && !Hooked)
			++S.m_Stuck;
		else
			S.m_Stuck = 0;
		if((TargetFrozen && Hazard(pCollision, Target) && pCollision->CheckPoint(Target + vec2(0, (pTarget ? pTarget->GetProximityRadius() : 28) + 4))) ||
			(g_Config.m_NullSerialGiveup > 0 && S.m_Stuck >= g_Config.m_NullSerialGiveup))
		{
			S.m_Target = -1;
			S.m_Locked = false;
			S.m_Stuck = 0;
			if(g_Config.m_NullSerialAdvance)
				S.m_Target = Pick(pClient, Warlist, Slot, Mode);
			return;
		}
		if(g_Config.m_NullSerialShove)
		{
			const float Range = TargetFrozen ? g_Config.m_NullSerialCarry : g_Config.m_NullSerialShoveRange;
			auto Goal = FreezeGoal(pCollision, Target, Range, TargetFrozen);
			if(!Goal && TargetFrozen)
				Goal = FreezeGoal(pCollision, Target, Range, false);
			if(S.m_HasHazard && (S.m_HazardFrozen != TargetFrozen || distance(S.m_Hazard, Target) > Range || !Hazard(pCollision, S.m_Hazard) || (TargetFrozen && !Lasting(pCollision, S.m_Hazard))))
				S.m_HasHazard = false;
			if(Goal && (!S.m_HasHazard || distance(*Goal, Target) <= distance(S.m_Hazard, Target) * .75f))
			{
				S.m_Hazard = *Goal;
				S.m_HasHazard = true;
				S.m_HazardFrozen = TargetFrozen;
			}
		}
		else
			S.m_HasHazard = false;
		const vec2 Push = S.m_HasHazard && distance(S.m_Hazard, Target) > .001f ? normalize(S.m_Hazard - Target) : vec2(0, 0);
		const bool HasPush = Push != vec2(0, 0);
		vec2 Stand = Target;
		bool HaveStand = false;
		float BestScore = 0, BestRisk = 0;
		CSerialThreats Foresight;
		Foresight.Build(World, Id, S.m_Target, g_Config.m_NullSerialForesightRange, g_Config.m_NullGodmodeReact, g_Config.m_NullGodmodeRocketThrow, Idle, pClient);
		const float Radius = Hooked ? 96.f : 42.f;
		for(int Ring = 0; Ring < 3; ++Ring)
			for(int Ray = 0; Ray < 12; ++Ray)
			{
				const float A = Ray * 6.2831855f / 12;
				const vec2 Out(std::cos(A), std::sin(A));
				const vec2 Candidate = Target + Out * Radius * (Ring == 0 ? 1.f : Ring == 1 ? 1.5f :
													      2.1f);
				if(pCollision->CheckPoint(Candidate) || Hazard(pCollision, Candidate))
					continue;
				float Score = (1.f - Ring * .5f) * g_Config.m_NullSerialPress;
				if(S.m_HasHazard)
				{
					Score += dot(Push, Out) * g_Config.m_NullSerialHaul * (Hooked ? 1.6f : 1.f) * (TargetFrozen ? 1.5f : 1.f);
					if(!Hooked && !TargetFrozen && Ring == 0)
						Score += dot(Unit(-Out + vec2(0, -1.1f)), Push) * 100.f;
					if(Push.y > .4f)
						Score += Out.y < -.5f ? 90.f : -60.f;
				}
				const float SafeRange = g_Config.m_NullSerialSafeRange;
				if(SafeRange > 0)
				{
					float Nearest = SafeRange;
					vec2 Near = Candidate;
					for(float Y = -SafeRange; Y <= SafeRange; Y += 32)
						for(float X = -SafeRange; X <= SafeRange; X += 32)
							if(Hazard(pCollision, Candidate + vec2(X, Y)) && length(vec2(X, Y)) < Nearest)
							{
								Nearest = length(vec2(X, Y));
								Near = Candidate + vec2(X, Y);
							}
					Score -= (1 - std::clamp(Nearest / SafeRange, 0.f, 1.f)) * g_Config.m_NullSerialSafety;
					if(Near.y > Candidate.y + 8)
						Score -= g_Config.m_NullSerialSafety * .5f;
				}
				const float Risk = Foresight.Risk(Candidate);
				Score -= g_Config.m_NullSerialForesight * Risk;
				if(!pCollision->IntersectLine(Position, Candidate, nullptr, nullptr))
					Score += 25;
				if(S.m_HaveStand && distance(S.m_Stand, Candidate) < Radius * .5f)
					Score += 15;
				if(!HaveStand || ((BestRisk > .9f) && Risk <= .9f) || ((BestRisk > .9f) == (Risk > .9f) && Score > BestScore))
				{
					Stand = Candidate;
					BestScore = Score;
					BestRisk = Risk;
					HaveStand = true;
				}
			}
		S.m_HaveStand = HaveStand;
		if(HaveStand)
			S.m_Stand = Stand;
		const vec2 Goal = (HaveStand ? Stand : Target) + S.m_Velocity * (g_Config.m_NullSerialLead * std::clamp((D - 120.f) / 240.f, 0.f, 1.f));
		const vec2 RouteGoal = HaveStand ? Stand : Target;
		if(Tick - S.m_LastRouteTick >= g_Config.m_NullSerialPathInterval || distance(RouteGoal, S.m_RouteGoal) > 150)
		{
			S.m_LastRouteTick = Tick;
			S.m_RouteGoal = RouteGoal;
			if(!S.m_Route.Build(pCollision, Position, HaveStand ? Stand : Target, g_Config.m_NullFlymodePathNodes, true))
				S.m_Route.Build(pCollision, Position, Target, g_Config.m_NullFlymodePathNodes, true);
		}
		if(S.m_Route.Valid())
			S.m_Route.UpdateCursor(Position);
		const vec2 Waypoint = S.m_Route.Goal(pCollision, Position, Goal, 2000, true);
		const vec2 Delta = Waypoint - Position;
		auto &Plan = S.m_Plan;
		Plan = {};
		const float Dx = D <= 60 && HaveStand ? Stand.x - Position.x : Delta.x;
		const float Deadzone = D <= 60 ? 2.f : 8.f;
		Plan.m_Direction = std::abs(Dx) > Deadzone ? (Dx > 0 ? 1 : -1) : 0;
		const bool Grounded = pSelf->IsGrounded();
		const bool Blocked = !S.m_Route.Valid() && pCollision->IntersectLine(Position, Goal, nullptr, nullptr);
		const bool StepUp = Plan.m_Direction && Grounded && pCollision->CheckPoint(Position + vec2(Plan.m_Direction * 34, 0)) && !pCollision->CheckPoint(Position + vec2(Plan.m_Direction * 34, -34));
		Plan.m_Jump = ((!Blocked && Delta.y < -20) || StepUp) && (Grounded || !(pSelf->Core()->m_Jumped & 2));
		vec2 Aim = Target - Position;
		if(S.m_Release > 0)
			--S.m_Release;
		if(g_Config.m_NullSerialHook)
		{
			const auto *pCore = pSelf->Core();
			const float HookLength = pSelf->GetTuning(pSelf->GetPureTuneZone())->m_HookLength;
			if(pCore->m_HookState == HOOK_GRABBED)
			{
				if(Hooked)
				{
					Plan.m_Hook = S.m_Release == 0;
					const float Speed = length(S.m_Velocity);
					// 24245b..2424f0 releases a completed throw independently of
					// firing: only fast motion toward the selected nearby hazard.
					if(HasPush && Speed > 5 && dot(S.m_Velocity / Speed, Push) > .55f && distance(Target, S.m_Hazard) < Speed * 10)
					{
						S.m_Release = 8;
						Plan.m_Hook = 0;
					}
				}
				else
				{
					const vec2 Anchor = pCore->m_HookPos - Position;
					Plan.m_Hook = D > 60 && length(Anchor) > 48 && (dot(Unit(Delta), Unit(Anchor)) > .2f || (Anchor.y < -length(Anchor) * .3f && Delta.y < -20));
				}
			}
			else if(pCore->m_HookState == HOOK_FLYING)
				Plan.m_Hook = 1;
			else if(pCore->m_HookState == HOOK_IDLE && !S.m_Release && D > 30 && D < HookLength && !pCollision->IntersectLine(Position, Target, nullptr, nullptr))
			{
				vec2 Lead = S.m_Velocity * (D / 80.f) * .5f;
				if(length(Lead) > 48)
					Lead = normalize(Lead) * 48;
				Aim += Lead;
				Plan.m_Hook = 1;
			}
			else if(D > 60 && pCore->m_HookState == HOOK_IDLE)
			{
				if(const auto Anchor = FindHookAnchor(pCollision, Position, Delta, HookLength))
				{
					Aim = *Anchor;
					Plan.m_Hook = 1;
				}
			}
		}
		const bool Hammer = g_Config.m_NullSerialHammer && !(g_Config.m_NullSerialWakeGuard && TargetFrozen) && pSelf->GetWeaponGot(WEAPON_HAMMER);
		bool Grenade = g_Config.m_NullSerialGrenade && pSelf->GetWeaponGot(WEAPON_GRENADE) && pSelf->GetWeaponAmmo(WEAPON_GRENADE) != 0 && (D <= 60 || !pCollision->IntersectLine(Position, Target, nullptr, nullptr));
		const float Closing = -dot(Unit(Target - Position), pSelf->Core()->m_Vel - S.m_Velocity);
		if(Closing > .5f && D > 60 && (D - 60) / Closing < 25)
			Grenade = false;
		int Weapon = -1;
		if(S.m_HasHazard && Push.y > .4f && Grenade)
			Weapon = WEAPON_GRENADE;
		else if(D <= 60 && Hammer)
			Weapon = Grenade && S.m_LastWeapon == WEAPON_HAMMER ? WEAPON_GRENADE : WEAPON_HAMMER;
		else if(Grenade)
			Weapon = WEAPON_GRENADE;
		else if(Hammer)
			Weapon = WEAPON_HAMMER;
		if(Weapon >= 0)
		{
			Plan.m_WantedWeapon = Weapon + 1;
			vec2 AttackAim = Unit(Target - Position);
			if(S.m_HasHazard && Weapon == WEAPON_HAMMER)
				AttackAim = Unit(AttackAim + vec2(0, -1.1f));
			const float Threshold = Weapon == WEAPON_HAMMER && !Hooked ? -.1f : .25f;
			const bool Aligned = !HasPush || dot(AttackAim, Push) > Threshold;
			if(pSelf->GetActiveWeapon() == Weapon && pSelf->DiagnosticReloadTimer() == 0 && Aligned)
			{
				Plan.m_Fire = 1;
				S.m_LastWeapon = Weapon;
				if(Hooked)
				{
					S.m_Release = 8;
					Plan.m_Hook = 0;
				}
				if(Weapon == WEAPON_HAMMER && Grenade)
					Plan.m_WantedWeapon = WEAPON_GRENADE + 1;
			}
		}
		Aim = Unit(Aim) * std::max(100.f, length(Aim));
		Plan.m_TargetX = Aim.x;
		Plan.m_TargetY = Aim.y;
		S.m_HasPlan = true;
	}
	CNetObj_PlayerInput CSerial::Apply(CGameClient *pClient, CNetObj_PlayerInput Input, int Slot, bool Consume)
	{
		auto &S = m_aStates[Slot];
		if(!S.m_HasPlan || S.m_Id != pClient->m_aLocalIds[Slot] || !(Slot == g_Config.m_ClDummy ? g_Config.m_NullSerialKiller : g_Config.m_NullSerialKillerDummy))
			return Input;
		Input.m_Direction = S.m_Plan.m_Direction;
		Input.m_Jump = S.m_Plan.m_Jump;
		Input.m_Hook = S.m_Plan.m_Hook;
		Input.m_TargetX = S.m_Plan.m_TargetX;
		Input.m_TargetY = S.m_Plan.m_TargetY;
		if(S.m_Plan.m_WantedWeapon)
			Input.m_WantedWeapon = S.m_Plan.m_WantedWeapon;
		if(S.m_Plan.m_Fire)
		{
			Input.m_Fire = (unsigned)Input.m_Fire + 2U;
			if(Consume)
				S.m_Plan.m_Fire = 0;
		}
		return Input;
	}
}
