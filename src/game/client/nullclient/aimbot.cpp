#include "aimbot.h"
#include <engine/shared/client_profile.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenKrx::NullClient
{
int SolveBallistic(vec2 From, vec2 To, float Curvature, float Speed, float TickSpeed, std::array<CBallisticAim, 2> &aResult)
{
	const vec2 Delta = To - From;
	const float Curve = Curvature / 10000.0f;
	const float TimeScale = Speed <= 0.001f ? 0.0f : TickSpeed / Speed;
	int Count = 0;
	if(std::abs(Curve) < 1e-9f)
	{
		const float Distance = length(Delta);
		if(Distance < 0.001f)
			return 0;
		aResult[0] = {Delta / Distance, Distance * TimeScale};
		return 1;
	}
	if(std::abs(Delta.x) >= 0.001f)
	{
		const float Discriminant = 1.0f - (Delta.x * Curve * Delta.x - Delta.y) * Curve * 4.0f;
		if(Discriminant < 0)
			return 0;
		const float Root = std::sqrt(Discriminant);
		const float Denominator = (Curve + Curve) * Delta.x;
		const float Sign = Delta.x > 0 ? 1.0f : -1.0f;
		for(float Numerator : {Root - 1.0f, -Root - 1.0f})
		{
			const float Slope = Numerator / Denominator;
			const float X = Sign / std::sqrt(Slope * Slope + 1.0f);
			if(Delta.x / X > 0.001f)
				aResult[Count++] = {vec2(X, X * Slope), (Delta.x / X) * TimeScale};
		}
	}
	else
	{
		const float Discriminant = Curve * 4.0f * Delta.y + 1.0f;
		if(Discriminant < 0)
			return 0;
		const float Root = std::sqrt(Discriminant);
		for(int Direction : {-1, 1})
			for(float Sign : {1.0f, -1.0f})
			{
				const float Range = (Sign * Root - static_cast<float>(Direction)) / (Curve + Curve);
				if(Range > 0.001f)
				{
					aResult[Count++] = {vec2(0, Direction), Range * TimeScale};
					if(Count == 2)
						break;
				}
			}
	}
	if(Count == 2 && aResult[1].m_Ticks < aResult[0].m_Ticks)
		std::swap(aResult[0], aResult[1]);
	return Count;
}

namespace
{
struct CWeapon
{
	bool m_Enabled;
	int m_Fov, m_Priority;
	bool m_Knock;
};
CWeapon WeaponSettings(int Weapon)
{
	switch(Weapon)
	{
	case -1: return {g_Config.m_NullAimbotHook != 0, g_Config.m_NullAimbotFovHook, g_Config.m_NullAimbotPriorityHook, false};
	case WEAPON_HAMMER: return {g_Config.m_NullAimbotHammer != 0, g_Config.m_NullAimbotFovHammer, g_Config.m_NullAimbotPriorityHammer, false};
	case WEAPON_GUN: return {g_Config.m_NullAimbotGun != 0, g_Config.m_NullAimbotFovGun, g_Config.m_NullAimbotPriorityGun, g_Config.m_NullAimbotKnockGun != 0};
	case WEAPON_SHOTGUN: return {g_Config.m_NullAimbotShotgun != 0, g_Config.m_NullAimbotFovShotgun, g_Config.m_NullAimbotPriorityShotgun, g_Config.m_NullAimbotKnockShotgun != 0};
	case WEAPON_GRENADE: return {g_Config.m_NullAimbotGrenade != 0, g_Config.m_NullAimbotFovGrenade, g_Config.m_NullAimbotPriorityGrenade, g_Config.m_NullAimbotKnockGrenade != 0};
	case WEAPON_LASER: return {g_Config.m_NullAimbotLaser != 0, g_Config.m_NullAimbotFovLaser, g_Config.m_NullAimbotPriorityLaser, g_Config.m_NullAimbotKnockLaser != 0};
	default: return {false, g_Config.m_NullAimbotFov, g_Config.m_NullAimbotPriority, false};
	}
}
float Reach(const CTuningParams &T, int Weapon, float Radius, bool DDRace)
{
	switch(Weapon)
	{
	case -1: return static_cast<float>(T.m_HookLength) + 28.0f;
	case WEAPON_HAMMER: return Radius * 1.25f + 28.0f + 16.0f;
	case WEAPON_GUN: return static_cast<float>(T.m_GunSpeed) * static_cast<float>(T.m_GunLifetime);
	case WEAPON_SHOTGUN: return DDRace ? static_cast<float>(T.m_LaserReach) : static_cast<float>(T.m_ShotgunSpeed) * static_cast<float>(T.m_ShotgunLifetime);
	case WEAPON_GRENADE: return static_cast<float>(T.m_GrenadeSpeed) * static_cast<float>(T.m_GrenadeLifetime);
	case WEAPON_LASER: return T.m_LaserReach;
	default: return g_Config.m_NullAimbotRange;
	}
}
vec2 TargetPosition(CGameClient *pClient, CGameWorld &World, int Target)
{
	if(Target < 0 || Target >= MAX_CLIENTS) return vec2(0, 0);
	const auto *pCharacter = World.GetCharacterById(Target);
	return pCharacter ? pCharacter->Core()->m_Pos : pClient->m_aClients[Target].m_Predicted.m_Pos;
}
float PingLead(CGameClient *pClient)
{
	if(g_Config.m_NullPingLead < 1 || !pClient->m_Snap.m_pLocalInfo)
		return 0;
	return std::clamp(pClient->m_Snap.m_pLocalInfo->m_Latency * 50.0f / 1000.0f, 0.0f, static_cast<float>(g_Config.m_NullPingLead));
}
vec2 PredictTarget(CGameClient *pClient, CGameWorld &World, int Target, vec2 Position, float Ticks, const CTuningParams &Tune)
{
	const auto *pCharacter = World.GetCharacterById(Target);
	vec2 Velocity = pCharacter ? pCharacter->Core()->m_Vel : vec2(pClient->m_Snap.m_aCharacters[Target].m_Cur.m_VelX, pClient->m_Snap.m_aCharacters[Target].m_Cur.m_VelY) / 256.0f;
	for(int Tick = 0; Tick < std::min(100, static_cast<int>(Ticks)); ++Tick)
	{
		Velocity.y += Tune.m_Gravity;
		if(pCharacter && pCharacter->Core()->m_HookState == HOOK_GRABBED && pCharacter->Core()->HookedPlayer() == -1)
		{
			const vec2 Delta = pCharacter->Core()->m_HookPos - Position;
			if(length(Delta) > 46.0f)
			{
				vec2 Pull = normalize(Delta) * static_cast<float>(Tune.m_HookDragAccel);
				if(Pull.y > 0)
					Pull.y *= 0.3f;
				const int Direction = Velocity.x > .5f ? 1 : Velocity.x < -.5f ? -1 : 0;
				Pull.x *= (Pull.x < 0 && Direction < 0) || (Pull.x > 0 && Direction > 0) ? .95f : .75f;
				const vec2 NewVelocity = Velocity + Pull;
				if(length(NewVelocity) < Tune.m_HookDragSpeed || length(NewVelocity) < length(Velocity))
					Velocity = NewVelocity;
			}
		}
		vec2 Collision;
		if(World.Collision()->IntersectLine(Position, Position + Velocity, &Collision, nullptr))
			return Collision;
		Position += Velocity;
	}
	return Position;
}
}

bool CAimbot::BounceAim(CCollision *pCollision, vec2 From, vec2 Target, float Radius, float Reach, float BounceCost, int Rays, vec2 &Aim)
{
	bool Found = false;
	int BestBounces = 9;
	float BestDistance = Radius;
	Rays = std::clamp(Rays, 24, 360);
	for(int Ray = 0; Ray < Rays; ++Ray)
	{
		const float Angle = Ray * 6.2831855f / Rays;
		const vec2 Initial(std::cos(Angle), std::sin(Angle));
		vec2 Position = From, Direction = Initial;
		float Remaining = Reach;
		for(int Bounce = 0; Bounce < 9 && Remaining > 0; ++Bounce)
		{
			vec2 Hit, Before;
			const vec2 End = Position + Direction * Remaining;
			const bool Wall = pCollision->IntersectLineTeleWeapon(Position, End, &Hit, &Before) != 0;
			const vec2 Stop = Wall ? Hit : End;
			if(Bounce)
			{
				const vec2 Delta = Stop - Position;
				const float Len2 = dot(Delta, Delta);
				const vec2 Closest = Position + Delta * (Len2 > 0 ? std::clamp(dot(Target - Position, Delta) / Len2, 0.0f, 1.0f) : 0.0f);
				const float Distance = distance(Target, Closest);
				if(Distance <= Radius && (Bounce < BestBounces || (Bounce == BestBounces && Distance < BestDistance)))
				{
					Found = true; Aim = Initial; BestBounces = Bounce; BestDistance = Distance;
					break;
				}
			}
			if(!Wall)
				break;
			vec2 NewPosition = Before, NewDirection = Direction * 4.0f;
			pCollision->MovePoint(&NewPosition, &NewDirection, 1.0f, nullptr);
			if(length(NewDirection) < .001f || distance(Position, NewPosition) < .001f)
				break;
			Remaining -= distance(Position, NewPosition) + BounceCost;
			Position = NewPosition; Direction = normalize(NewDirection);
		}
	}
	return Found;
}

CNetObj_PlayerInput CAimbot::Apply(CGameClient *pClient, CNetObj_PlayerInput Input, int Slot, bool AvoidChanged)
{
	if(!NullProfileActive() || Slot < 0 || Slot > 1)
		return Input;
	auto &State = m_aStates[Slot];
	const int PreviousFire = State.m_LastFire, PreviousTarget = State.m_StickyTarget;
	State.m_LastFire = Input.m_Fire; State.m_Target = State.m_Weapon = -1;
	const bool HoldHook = g_Config.m_NullBindAimbotHook && pClient->Input()->KeyIsPressed(g_Config.m_NullBindAimbotHook);
	if((!g_Config.m_NullAimbot && !HoldHook) || AvoidChanged || pClient->Client()->State() != IClient::STATE_ONLINE)
		return Input;
	auto &World = pClient->KrxPredictedWorld();
	const int Id = pClient->m_aLocalIds[Slot];
	auto *pSelf = World.GetCharacterById(Id);
	if(!pSelf)
		return Input;
	if(HoldHook)
	{
		if(pSelf->Core()->m_HookState == HOOK_GRABBED)
		{
			Input.m_Hook = pSelf->Core()->HookedPlayer() >= 0 ? 1 : 0;
			return Input;
		}
		if(pSelf->Core()->m_HookState == HOOK_FLYING)
		{
			Input.m_Hook = 1;
			return Input;
		}
		if(pSelf->Core()->m_HookState != HOOK_IDLE)
		{
			Input.m_Hook = 0;
			return Input;
		}
	}
	const bool Hook = HoldHook || (Input.m_Hook && g_Config.m_NullAimbotHook);
	const int Weapon = Hook ? -1 : pSelf->GetActiveWeapon();
	const auto Settings = WeaponSettings(Weapon);
	if((!Settings.m_Enabled && !HoldHook) || (Input.m_Hook && !g_Config.m_NullAimbotHook && !HoldHook))
		return Input;
	(void)PreviousFire;
	const auto &Tune = *pSelf->GetTuning(pSelf->GetPureTuneZone());
	const bool DDRace = World.m_WorldConfig.m_IsDDRace;
	const bool Projectile = Weapon == WEAPON_GUN || Weapon == WEAPON_GRENADE || (Weapon == WEAPON_SHOTGUN && !DDRace);
	const float Curvature = Weapon == WEAPON_GUN ? Tune.m_GunCurvature : Weapon == WEAPON_GRENADE ? Tune.m_GrenadeCurvature : Tune.m_ShotgunCurvature;
	const float Speed = Weapon == WEAPON_GUN ? Tune.m_GunSpeed : Weapon == WEAPON_GRENADE ? Tune.m_GrenadeSpeed : Tune.m_ShotgunSpeed;
	const float Range = Reach(Tune, Weapon, pSelf->GetProximityRadius(), DDRace);
	const float Fov = std::cos((std::min(180.0f, Settings.m_Fov * .5f) * 3.1415927f) / 180.0f);
	vec2 Mouse = pClient->m_Controls.m_aMousePos[Slot];
	Mouse = length(Mouse) < .001f ? vec2(1, 0) : normalize(Mouse);
	const vec2 Position = pSelf->Core()->m_Pos;
	int Target = -1;
	float BestScore = 0;
	for(int Other = 0; Other < MAX_CLIENTS; ++Other)
	{
		if(Other == Id || !pClient->m_Snap.m_aCharacters[Other].m_Active || (g_Config.m_NullAimbotIgnoreFriends && pClient->m_aClients[Other].m_Friend))
			continue;
		const auto *pOther = World.GetCharacterById(Other);
		const float Radius = pOther ? pOther->GetProximityRadius() : 28.0f;
		const vec2 OtherPosition = TargetPosition(pClient, World, Other);
		const vec2 Difference = OtherPosition - Position;
		const float Distance = length(Difference);
		if(Distance < .001f || Range < Distance - Radius)
			continue;
		const float Facing = dot(Mouse, Difference / Distance);
		if(Facing < Fov)
			continue;
		if(Projectile)
		{
			std::array<CBallisticAim, 2> Shots;
			vec2 Endpoint = OtherPosition;
			if(Weapon == WEAPON_GRENADE && Distance > 70)
				Endpoint -= Difference / Distance * 70.0f;
			if(!SolveBallistic(Position + Difference / Distance * pSelf->GetProximityRadius() * .75f, Endpoint, Curvature, Speed, 50, Shots))
				continue;
		}
		else if(Weapon == -1 || g_Config.m_NullAimbotLineOfSight)
		{
			vec2 Hit;
			if(World.Collision()->IntersectLine(Position, OtherPosition, &Hit, nullptr) && distance(Hit, OtherPosition) > Radius &&
				!((Weapon == WEAPON_LASER || (Weapon == WEAPON_SHOTGUN && DDRace)) && g_Config.m_NullAimbotWallbounce))
				continue;
		}
		float Score = Settings.m_Priority == 1 ? Distance : 1.0f - Facing;
		if(g_Config.m_NullAimbotSticky && Other == PreviousTarget)
			Score *= 1.0f - g_Config.m_NullAimbotStickyMargin / 100.0f;
		if(Target < 0 || Score < BestScore)
			Target = Other, BestScore = Score;
	}
	State.m_StickyTarget = Target;
	if(Target < 0 || Target >= MAX_CLIENTS)
		return Input;
	State.m_Target = Target;
	State.m_Weapon = Weapon;
	const auto *pTarget = World.GetCharacterById(Target);
	const float Radius = pTarget ? pTarget->GetProximityRadius() : 28.0f;
	vec2 TargetPos = TargetPosition(pClient, World, Target);
	if(pTarget)
		TargetPos += pTarget->Core()->m_Vel * PingLead(pClient);
	vec2 Aim = normalize(TargetPos - Position);
	bool Found = false;
	float Estimate = 0;
	for(int Pass = 0; Pass < 3; ++Pass)
	{
		const vec2 Start = Projectile ? Position + Aim * pSelf->GetProximityRadius() * .75f : Position;
		const vec2 Point = g_Config.m_NullAimbotLead ? PredictTarget(pClient, World, Target, TargetPos, Estimate, Tune) : TargetPos;
		if(Projectile)
		{
			std::array<CBallisticAim, 2> Shots;
			int Count = SolveBallistic(Start, Point, Curvature, Speed, 50, Shots);
			if(!Count && Weapon == WEAPON_GRENADE && distance(Start, Point) > 70.0f)
				Count = SolveBallistic(Start, Point - normalize(Point - Start) * 70.0f, Curvature, Speed, 50, Shots);
			Found = false;
			for(int Shot = 0; Shot < Count; ++Shot)
			{
				if(Shots[Shot].m_Ticks > 100 || (Shot > 0 && !g_Config.m_NullAimbotArc))
					continue;
				const float Time = Shots[Shot].m_Ticks / 50.0f;
				const int Steps = std::clamp(static_cast<int>(Speed * Time / 12.0f), 1, 400);
				vec2 Previous = Start;
				bool Clear = true;
				for(int Step = 1; Step <= Steps; ++Step)
				{
					const float RangeAt = (Step * Time / Steps) * Speed;
					const vec2 Next = Start + Shots[Shot].m_Direction * RangeAt + vec2(0, RangeAt * RangeAt * (Curvature / 10000.0f));
					if(World.Collision()->IntersectLine(Previous, Next, nullptr, nullptr))
					{
						Clear = Weapon == WEAPON_GRENADE && distance(Previous, Point) < 70.0f;
						break;
					}
					Previous = Next;
				}
				if(Clear)
				{
					Aim = Shots[Shot].m_Direction;
					Estimate = Shots[Shot].m_Ticks;
					Found = true;
					break;
				}
			}
			if(!Found)
				break;
		}
		else
		{
			Aim = normalize(Point - Start);
			Found = true;
			vec2 Hit;
			if(Radius > 4 && World.Collision()->IntersectLine(Start, Point, &Hit, nullptr) && distance(Hit, Point) > 2)
			{
				float Best = -1;
				for(int Ray = 0; Ray < 16; ++Ray)
				{
					const float Angle = Ray * 6.2831855f / 16;
					const vec2 Edge = Point + vec2(std::cos(Angle), std::sin(Angle)) * Radius * .9f;
					if(!World.Collision()->IntersectLine(Start, Edge, &Hit, nullptr) || distance(Hit, Edge) <= 6)
						if(Best < 0 || distance(Start, Edge) < Best)
							Best = distance(Start, Edge), Aim = normalize(Edge - Start);
				}
			}
			if((Weapon == WEAPON_LASER || (Weapon == WEAPON_SHOTGUN && DDRace)) && g_Config.m_NullAimbotWallbounce && World.Collision()->IntersectLine(Start, Point, nullptr, nullptr))
				BounceAim(World.Collision(), Start, Point, Radius, Range, Tune.m_LaserBounceCost, g_Config.m_NullAimbotWallbounceRays, Aim);
			if(Weapon != -1)
				break;
			Estimate = std::min(static_cast<float>(Tune.m_HookLength), distance(Start, Point)) / std::max(1.0f, static_cast<float>(Tune.m_HookFireSpeed));
		}
	}
	if(!Found)
	{
		State.m_Target = -1;
		return Input;
	}
	const float Distance = distance(Position, TargetPos);
	if(Settings.m_Knock && Distance > .001f && Distance <= g_Config.m_NullAimbotKnockRange)
		Aim = normalize(TargetPos - Position);
	Input.m_TargetX = static_cast<int>(Aim.x * std::max(100.0f, Distance));
	Input.m_TargetY = static_cast<int>(Aim.y * std::max(100.0f, Distance));
	if(HoldHook && Weapon == -1)
		Input.m_Hook = 1;
	return Input;
}
}
