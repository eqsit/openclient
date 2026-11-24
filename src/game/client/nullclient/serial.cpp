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
#include <numbers>

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
		bool RouteReachable(CCollision *pCollision, vec2 From, vec2 To)
		{
			// 0x140280480 checks the centre and four corners of the tee.
			for(const vec2 Offset : {vec2(0, 0), vec2(-14, -14), vec2(14, -14), vec2(-14, 14), vec2(14, 14)})
				if(!CRoute::SegmentSafe(pCollision, From + Offset, To + Offset, true))
					return false;
			return true;
		}
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
			// 0x14027cf20:158-175: hazard point within range whose line to the
			// target is free. A point with two or more blocked neighbours beats
			// any clean one once found; inside a class the nearest wins and
			// k==4 is allowed.
			std::optional<vec2> Best;
			float BestDistance = std::numeric_limits<float>::max();
			bool BestBlocked = false, Have = false;
			for(float Y = -Range; Y <= Range; Y += 32)
				for(float X = -Range; X <= Range; X += 32)
				{
					const vec2 P = Target + vec2(X, Y);
					const float D = distance(P, Target);
					if(D > Range || !Hazard(pCollision, P) || (LastingOnly && !Lasting(pCollision, P)))
						continue;
					if(pCollision->IntersectLine(Target, P, nullptr, nullptr))
						continue;
					int Surrounded = 0;
					for(const vec2 Offset : {vec2(32, 0), vec2(-32, 0), vec2(0, 32), vec2(0, -32)})
						Surrounded += Hazard(pCollision, P + Offset) || pCollision->CheckPoint(P + Offset);
					const bool Blocked = Surrounded > 1;
					if(!Have || (Blocked && !BestBlocked) || (Blocked == BestBlocked && D < BestDistance))
					{
						Best = P;
						BestDistance = D;
						BestBlocked = Blocked;
						Have = true;
					}
				}
			return Best;
		}
	}
	std::optional<vec2> CSerial::FindHookAnchor(CCollision *pCollision, vec2 Position, vec2 Delta, float HookLength)
	{
		// 0x14027bd50 tests tee clearance up to 24 units before the hook point.
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
			if(pCollision->IntersectLineTeleHook(Position, Position + Direction * HookLength, &Hit, nullptr) != TILE_SOLID ||
				!RouteReachable(pCollision, Position, Hit - Direction * 24.0f))
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
			if(Group >= 0 && !Warlist.Contains(Group, pClient->m_aClients[Other].m_aName))
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
			const int PreviousFire = S.m_Id == Id ? S.m_Plan.m_Fire : 0;
			S = {};
			S.m_Id = Id;
			S.m_Plan.m_Fire = PreviousFire;
			return;
		}
		if(S.m_Id != Id)
		{
			S = {};
			S.m_Id = Id;
		}
		// Both Serial instances dispatch on the controlled clock (0x14027dff0).
		// Route age and navigation reuse use the instance slot (0x14027e990).
		const int UpdateTick = pClient->Client()->GameTick(g_Config.m_ClDummy);
		if(S.m_LastUpdateTick == UpdateTick)
			return;
		S.m_LastUpdateTick = UpdateTick;
		const int Tick = pClient->Client()->GameTick(Slot);
		S.m_LastTick = Tick;
		const bool PreviousJump = S.m_LastJump;
		S.m_LastJump = 0;
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
			S.m_NavigationTick = -1000;
		}
		auto *pTarget = World.GetCharacterById(TargetId);
		const vec2 Position = pSelf->GetPos();
		const vec2 Target = pTarget ? pTarget->GetPos() : pClient->m_aClients[TargetId].m_RenderPos;
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
					if(!Hooked && !TargetFrozen)
						Score += dot(Unit(-Out + vec2(0, -1.1f)), Push) * 100.f;
					if(!Hooked && !TargetFrozen && Push.y > .4f)
						Score += Out.y < -.5f ? 90.f : -60.f;
					if(Out.y < -.4f)
						Score += g_Config.m_NullSerialAbove;
				}
				const float SafeRange = g_Config.m_NullSerialSafeRange;
				if(SafeRange > 0)
				{
					float Nearest = std::numeric_limits<float>::infinity();
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
		const float RepathDistance = std::max(64.0f, 150.0f - length(S.m_Velocity) * 4.0f);
		if(Tick - S.m_LastRouteTick >= g_Config.m_NullSerialPathInterval || distance(RouteGoal, S.m_RouteGoal) > RepathDistance)
		{
			S.m_LastRouteTick = Tick;
			S.m_RouteGoal = RouteGoal;
			const int NodeLimit = g_Config.m_NullFlymodePathNodes;
			if(!S.m_Route.Build(pCollision, Position, RouteGoal, NodeLimit, true) &&
				(!HaveStand || !S.m_Route.Build(pCollision, Position, Target, NodeLimit, true)))
			{
				// The nearest usable ring point is an endpoint for a fresh route search.
				std::optional<vec2> RingGoal;
				float BestDistance = std::numeric_limits<float>::infinity();
				for(int Ray = 0; Ray < 8; ++Ray)
				{
					const float Angle = Ray * (std::numbers::pi_v<float> / 4.0f);
					const vec2 Point = Target + vec2(std::cos(Angle), std::sin(Angle)) * 64.0f;
					const float Distance = distance(Position, Point);
					if(Distance < BestDistance && RouteReachable(pCollision, Point, Point))
					{
						RingGoal = Point;
						BestDistance = Distance;
					}
				}
				if(RingGoal)
					S.m_Route.Build(pCollision, Position, *RingGoal, NodeLimit, true);
			}
		}
		const bool ClearGoal = RouteReachable(pCollision, Position, Goal);
		vec2 Waypoint = Position;
		if(ClearGoal && Goal.y >= Position.y - 64.0f)
			Waypoint = Goal;
		else
		{
			const auto &vPoints = S.m_Route.m_vPoints;
			const auto It = std::find_if(vPoints.rbegin(), vPoints.rend(), [&](vec2 Point) {
				return distance(Position, Point) <= 160.0f && RouteReachable(pCollision, Position, Point);
			});
			if(It != vPoints.rend())
				Waypoint = *It;
		}
		const vec2 Up = Position + vec2(0, -96.0f);
		if(distance(Position, Waypoint) < 16.0f && distance(Position, Goal) > 64.0f && RouteReachable(pCollision, Position, Up))
			Waypoint = Up;

		const vec2 Delta = Waypoint - Position;
		auto &Plan = S.m_Plan;
		const int PreviousFire = Plan.m_Fire;
		Plan = {};
		Plan.m_Fire = PreviousFire;
		const float Dx = D <= 60 && HaveStand ? Stand.x - Position.x : Delta.x;
		const float Deadzone = D <= 60 ? 2.f : 8.f;
		Plan.m_Direction = std::abs(Dx) > Deadzone ? (Dx > 0 ? 1 : -1) : 0;
		const bool Grounded = pSelf->IsGrounded();
		const bool Blocked = Waypoint == Position && !ClearGoal;
		const bool StepUp = Plan.m_Direction && Grounded && pCollision->CheckPoint(Position + vec2(Plan.m_Direction * 34, 0)) && !pCollision->CheckPoint(Position + vec2(Plan.m_Direction * 34, -34));
		const bool WantJump = ((!Blocked && Delta.y < -20) || StepUp) && (Grounded || !(pSelf->Core()->m_Jumped & 2));
		Plan.m_Jump = WantJump;
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
		Aim = Unit(Aim) * std::max(100.f, length(Aim));
		Plan.m_TargetX = Aim.x;
		Plan.m_TargetY = Aim.y;
		const bool OpenAhead = Plan.m_Direction && !pCollision->CheckPoint(Position + vec2(Plan.m_Direction * 48, 18));
		const bool Navigating = g_Config.m_NullSerialNavigation && (!ClearGoal || StepUp || (D > 60 && (Delta.y < -20 || OpenAhead)));
		if(Navigating)
			Navigate(pClient, S, Waypoint, Target, Foresight, OpenAhead, Idle);
		else
			S.m_NavigationTick = -1000;
		// 0x14027e990:644-653 issues jump only after a tick without one.
		if(Plan.m_Jump && PreviousJump)
			Plan.m_Jump = 0;

		const bool Hammer = g_Config.m_NullSerialHammer && !(g_Config.m_NullSerialWakeGuard && TargetFrozen) && pSelf->GetWeaponGot(WEAPON_HAMMER);
		const bool ClearTarget = !pCollision->IntersectLine(Position, Target, nullptr, nullptr);
		bool Grenade = g_Config.m_NullSerialGrenade && pSelf->GetWeaponGot(WEAPON_GRENADE) && pSelf->GetWeaponAmmo(WEAPON_GRENADE) != 0 && (D <= 60 || ClearTarget);
		const vec2 TargetDirection = Unit(Target - Position);
		const float Closing = dot(TargetDirection, pSelf->Core()->m_Vel - S.m_Velocity);
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
			vec2 AttackAim = TargetDirection;
			if(S.m_HasHazard && Weapon == WEAPON_HAMMER)
				AttackAim = Unit(AttackAim + vec2(0, -1.1f));
			const float Threshold = Weapon == WEAPON_HAMMER && !Hooked ? -.1f : .25f;
			const bool Aligned = !HasPush || dot(AttackAim, Push) > Threshold;
			const bool HoldAim = Plan.m_Hook && pSelf->Core()->m_HookState == HOOK_IDLE;
			const vec2 PlannedAim(Plan.m_TargetX, Plan.m_TargetY);
			if(!HoldAim)
			{
				const vec2 AttackTarget = TargetDirection * std::max(100.f, D);
				Plan.m_TargetX = AttackTarget.x;
				Plan.m_TargetY = AttackTarget.y;
			}
			const bool InRange = Weapon != WEAPON_HAMMER || (D <= 60 && ClearTarget);
			const bool HookAligned = !HoldAim || dot(TargetDirection, Unit(PlannedAim)) > .98f;
			bool CanFire = pSelf->GetActiveWeapon() == Weapon && pSelf->DiagnosticReloadTimer() == 0 && Aligned && InRange && HookAligned;
			if(CanFire && Navigating && Weapon == WEAPON_GRENADE)
			{
				CSettings Settings;
				Settings.m_Weapons = true;
				Settings.m_Adversarial = 0;
				CSimulation Simulation;
				CanFire = Simulation.Initialize(World, Id, Settings, Idle, CPruneRadii{640.f, 1200.f, 800.f});
				if(CanFire)
				{
					auto Shot = Plan;
					Shot.m_Fire = static_cast<unsigned>(Plan.m_Fire) + 2U;
					for(int Step = 0; Step < 32; ++Step)
					{
						if(Step > 0)
							Shot.m_Jump = 0;
						if(Step >= 24)
							Shot.m_Direction = 0;
						if(!Simulation.Tick(Shot) || Simulation.Dead() || Simulation.Frozen())
						{
							CanFire = false;
							break;
						}
					}
				}
			}
			if(CanFire)
			{
				Plan.m_Fire = static_cast<unsigned>(Plan.m_Fire) + 2U;
				S.m_LastWeapon = Weapon;
				if(Hooked)
				{
					S.m_Release = 8;
					Plan.m_Hook = 0;
				}
			}
		}
		S.m_LastJump = Plan.m_Jump;
		S.m_HasPlan = true;
	}

	void CSerial::Navigate(CGameClient *pClient, CState &State, vec2 Goal, vec2 Target, const CSerialThreats &Threats, bool OpenAhead, const std::array<bool, MAX_CLIENTS> &Idle)
	{
		auto &Source = pClient->KrxPredictedWorld();
		auto *pSelf = Source.GetCharacterById(State.m_Id);
		const int HookState = pSelf->Core()->m_HookState;
		auto &Plan = State.m_Plan;
		auto &BestInput = State.m_NavigationInput;
		const bool Reuse = State.m_LastTick >= State.m_NavigationTick && State.m_LastTick - State.m_NavigationTick < 3 &&
			HookState == State.m_NavigationHook && distance(Goal, State.m_NavigationGoal) <= 32.0f;
		if(!Reuse)
		{
			State.m_NavigationTick = State.m_LastTick;
			State.m_NavigationHook = HookState;
			State.m_NavigationGoal = Goal;
			BestInput = Plan;
			BestInput.m_Direction = BestInput.m_Jump = BestInput.m_Hook = 0;

			CSettings Settings;
			Settings.m_Horizon = 32;
			Settings.m_Adversarial = 0;
			CSimulation Simulation;
			if(Simulation.Initialize(Source, State.m_Id, Settings, Idle, CPruneRadii{640.f, 1200.f, 800.f}))
			{
				const vec2 Origin = Simulation.Position();
				const float HookLength = pSelf->GetTuning(pSelf->GetPureTuneZone())->m_HookLength;
				const auto Anchor = g_Config.m_NullSerialHook && HookState == HOOK_IDLE ?
					FindHookAnchor(pClient->Collision(), Origin, Goal - Origin, HookLength) : std::nullopt;
				const int JumpMax = !OpenAhead && (pSelf->IsGrounded() || !(pSelf->Core()->m_Jumped & 2));
				int BestLife = -1;
				float BestScore = 0.f;
				for(int HookChoice = 0; HookChoice < (Anchor ? 3 : 2); ++HookChoice)
				{
					// 0x14027d590:164-192 varies the hook: keep, drop (skipped
					// when not hooking) or press while aiming at the anchor.
					// The input packing puts m_Hook at +0x14 and m_Fire at +0x10.
					if(HookChoice == 1 && Plan.m_Hook == 0)
						continue;
					for(int Direction = -1; Direction <= 1; ++Direction)
						for(int Jump = 0; Jump <= JumpMax; ++Jump)
						{
							auto Input = Plan;
							Input.m_Direction = Direction;
							Input.m_Jump = Jump;
							Input.m_Hook = HookChoice == 0 ? Plan.m_Hook : HookChoice == 1 ? 0 : 1;
							if(HookChoice == 2)
							{
								Input.m_TargetX = round_to_int(Anchor->x);
								Input.m_TargetY = round_to_int(Anchor->y);
							}
							const auto First = Input;
							Simulation.Reset();
							vec2 End = Origin;
							vec2 DriveEnd = Origin;
							int Life = 0;
							float Risk = 0.f;
							for(; Life < 32; ++Life)
							{
								if(Life > 0)
									Input.m_Jump = 0;
								if(Life >= 24)
									Input.m_Direction = 0;
								if(!Simulation.Tick(Input) || Simulation.Dead() || Simulation.Frozen())
									break;
								End = Simulation.Position();
								if(Simulation.NearFreeze(24.f))
									Risk += 8.f;
								if(Life == 23)
									DriveEnd = End;
							}
							if(Life == 32)
								End = DriveEnd;
							float Score = -2.f * distance(End, Goal) - Risk - 6.f * Jump -
								Threats.Risk(End) * g_Config.m_NullSerialForesight;
							if(!pClient->Collision()->IntersectLine(End, Target, nullptr, nullptr))
								Score += 12.f;
							if(Simulation.Grounded())
								Score += 8.f;
							if(HookChoice == 2)
								Score -= 4.f;
							if(Life > BestLife || (Life == BestLife && Score > BestScore))
							{
								BestLife = Life;
								BestScore = Score;
								BestInput = First;
							}
						}
				}
			}
		}
		Plan.m_Direction = BestInput.m_Direction;
		Plan.m_Jump = Reuse ? 0 : BestInput.m_Jump;
		Plan.m_Hook = BestInput.m_Hook;
		if(Plan.m_Hook && HookState == HOOK_IDLE)
		{
			Plan.m_TargetX = BestInput.m_TargetX;
			Plan.m_TargetY = BestInput.m_TargetY;
		}
	}
	CNetObj_PlayerInput CSerial::Apply(CGameClient *pClient, CNetObj_PlayerInput Input, int Slot) const
	{
		const auto &S = m_aStates[Slot];
		if(!S.m_HasPlan || S.m_Id != pClient->m_aLocalIds[Slot] || !(Slot == g_Config.m_ClDummy ? g_Config.m_NullSerialKiller : g_Config.m_NullSerialKillerDummy))
			return Input;
		Input.m_Direction = S.m_Plan.m_Direction;
		Input.m_Jump = S.m_Plan.m_Jump;
		Input.m_Hook = S.m_Plan.m_Hook;
		Input.m_TargetX = S.m_Plan.m_TargetX;
		Input.m_TargetY = S.m_Plan.m_TargetY;
		Input.m_WantedWeapon = S.m_Plan.m_WantedWeapon;
		Input.m_Fire = S.m_Plan.m_Fire;
		return Input;
	}
}
