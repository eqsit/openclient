#include "aimbot.h"
#include <engine/shared/client_profile.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/localization.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace OpenKrx::NullClient
{
int SolveBallistic(vec2 From, vec2 To, float Curvature, float Speed, float TickSpeed, std::array<CBallisticAim, 2> &aResult)
{
	const vec2 Delta = To - From;
	const double Curve = static_cast<double>(Curvature) / 10000.0;
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
		const double Dx = Delta.x;
		const double CurvedY = Dx * Curve * Dx;
		const double DeltaY = Delta.y - CurvedY;
		const double Discriminant = 1.0 - (CurvedY - Delta.y) * Curve * 4.0;
		if(Discriminant < 0)
			return 0;
		const double Root = std::sqrt(Discriminant);
		const double Sign = Dx > 0 ? 1.0 : -1.0;
		// Rationalize the near root to avoid subtracting two almost equal values.
		for(double Slope : {(DeltaY + DeltaY) / (Dx * (Root + 1.0)), (-1.0 - Root) / (Dx * (Curve + Curve))})
		{
			const double X = Sign / std::sqrt(Slope * Slope + 1.0);
			const double Range = Dx / X;
			if(Range > 0.001f)
				aResult[Count++] = {vec2(X, X * Slope), static_cast<float>(Range * TimeScale)};
		}
	}
	else
	{
		const double Discriminant = Curve * 4.0 * Delta.y + 1.0;
		if(Discriminant < 0)
			return 0;
		const double Root = std::sqrt(Discriminant);
		for(int Direction : {-1, 1})
			for(double SignedRoot : {Root, -Root})
			{
				const double Range = (SignedRoot - Direction) / (Curve + Curve);
				if(Count < 2 && Range > 0.001f)
					aResult[Count++] = {vec2(0, Direction), static_cast<float>(Range * TimeScale)};
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
	case -1: return T.m_HookLength;
	case WEAPON_HAMMER: return Radius * 1.25f;
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
	return pCharacter ? pCharacter->Core()->m_Pos : pClient->m_aClients[Target].m_RenderPos;
}
float PingLead(CGameClient *pClient)
{
	if(g_Config.m_NullPingLead < 1 || !pClient->m_Snap.m_pLocalInfo)
		return 0;
	return std::clamp(pClient->m_Snap.m_pLocalInfo->m_Latency * 50.0f / 1000.0f, 0.0f, static_cast<float>(g_Config.m_NullPingLead));
}
struct CTargetPath
{
	std::array<vec2, 101> m_aPos;
	int m_LastTick;

	vec2 At(float Tick) const
	{
		Tick = std::clamp(Tick, 0.0f, static_cast<float>(m_LastTick));
		const int Index = static_cast<int>(Tick);
		return mix(m_aPos[Index], m_aPos[std::min(Index + 1, m_LastTick)], Tick - Index);
	}
};

CTargetPath TargetPath(CGameClient *pClient, CGameWorld &World, int Target, int Horizon, const CTuningParams &Tune, bool ReleaseHook = false)
{
	CTargetPath Path;
	Path.m_LastTick = Horizon;
	Path.m_aPos[0] = TargetPosition(pClient, World, Target);
	if(const auto *pCharacter = World.GetCharacterById(Target))
	{
		CCharacterCore Core = *pCharacter->Core();
		Core.Init(nullptr, World.Collision());
		if(ReleaseHook)
			Core.m_HookState = HOOK_IDLE;
		if(pCharacter->m_FreezeTime > 0 || Core.m_LiveFrozen)
			Core.m_Direction = Core.m_Input.m_Direction = 0;
		for(int Tick = 1; Tick <= Horizon; ++Tick)
		{
			Core.Tick(false);
			Core.Move();
			Core.Quantize();
			Path.m_aPos[Tick] = Core.m_Pos;
		}
	}
	else
	{
		const auto &Snap = pClient->m_Snap.m_aCharacters[Target].m_Cur;
		vec2 Velocity = vec2(Snap.m_VelX, Snap.m_VelY) / 256.0f;
		vec2 Position = Path.m_aPos[0];
		for(int Tick = 1; Tick <= Horizon; ++Tick)
		{
			Velocity.y += Tune.m_Gravity;
			World.Collision()->MoveBox(&Position, &Velocity, vec2(28, 28), vec2(0, 0));
			Path.m_aPos[Tick] = Position;
		}
	}
	return Path;
}

vec2 WireDirection(vec2 Direction)
{
	if(length(Direction) < 0.0001f)
		return vec2(1, 0);
	return normalize(vec2(round_to_int(Direction.x * 10000.0f), round_to_int(Direction.y * 10000.0f)));
}

std::vector<float> HookAngles(const CCollision &Collision, vec2 Origin, vec2 Target, float Radius, vec2 Mouse, float Fov)
{
	// 0x14021e7e0 orders the precise centre probes before the wider fan and tile corners.
	const vec2 Delta = Target - Origin;
	const float Distance = length(Delta);
	const float Centre = angle(Delta);
	const float Width = std::asin(std::min(.95f, (Radius + 1.5f) / Distance));
	std::vector<float> vAngles;
	vAngles.reserve(84);
	vAngles.push_back(Centre);
	for(float Offset : {0.000025f, 0.0001f, 0.0004f, 0.0015f, 0.005f, 0.015f})
	{
		vAngles.push_back(Centre + Offset);
		vAngles.push_back(Centre - Offset);
	}
	for(int Sample = 1; Sample <= 24; ++Sample)
	{
		const float Offset = Sample * Width / 24.0f;
		vAngles.push_back(Centre + Offset);
		vAngles.push_back(Centre - Offset);
	}
	vec2 Hit;
	if(Collision.IntersectLineTeleHook(Origin + (Delta / Distance) * Radius * 1.5f, Target, &Hit, nullptr))
	{
		const int X = round_to_int(Hit.x) / 32;
		const int Y = round_to_int(Hit.y) / 32;
		for(int Dx : {0, 1})
			for(int Dy : {0, 1})
			{
				const vec2 Corner((X + Dx) * 32.0f - .5f, (Y + Dy) * 32.0f - .5f);
				const float CornerAngle = angle(Corner - Origin);
				for(float Offset : {0.0f, -.0001f, .0001f, -.0005f, .0005f})
					vAngles.push_back(CornerAngle + Offset);
			}
	}
	const float Cursor = angle(Mouse);
	const float Edge = std::max(0.0f, std::acos(std::clamp(Fov, -1.0f, 1.0f)) - .0001f);
	vAngles.insert(vAngles.end(), {Cursor, Cursor - Edge, Cursor + Edge});
	return vAngles;
}

bool CanTarget(CGameClient *pClient, CGameWorld &World, const CCharacter *pSelf, int SelfId, int Target, int Weapon, const CTuningParams &Tune)
{
	const auto &Data = pClient->m_aClients[Target];
	if(Target == SelfId || !pClient->m_Snap.m_aCharacters[Target].m_Active || Data.m_Paused ||
		Data.m_Team == TEAM_SPECTATORS)
		return false;
	auto *pOther = World.GetCharacterById(Target);
	const auto &SelfData = pClient->m_aClients[SelfId];
	const bool SelfSuper = pSelf ? pSelf->Core()->m_Super : SelfData.m_Super;
	const bool SelfSolo = pSelf ? pSelf->Core()->m_Solo : SelfData.m_Solo;
	if(!SelfSuper && !(pOther ? pOther->Core()->m_Super : Data.m_Super))
	{
		if(!(pOther ? pOther->CanCollide(SelfId) : pClient->m_Teams.CanCollide(SelfId, Target)) ||
			SelfSolo || (pOther ? pOther->Core()->m_Solo : Data.m_Solo))
			return false;
	}
	// Without an entity the client-data copy of the same flags is the only source.
	const bool HammerHitDisabled = pSelf ? pSelf->Core()->m_HammerHitDisabled : SelfData.m_HammerHitDisabled;
	const bool ShotgunHitDisabled = pSelf ? pSelf->Core()->m_ShotgunHitDisabled : SelfData.m_ShotgunHitDisabled;
	const bool LaserHitDisabled = pSelf ? pSelf->Core()->m_LaserHitDisabled : SelfData.m_LaserHitDisabled;
	const bool GrenadeHitDisabled = pSelf ? pSelf->Core()->m_GrenadeHitDisabled : SelfData.m_GrenadeHitDisabled;
	const bool HookHitDisabled = pSelf ? pSelf->Core()->m_HookHitDisabled : SelfData.m_HookHitDisabled;
	switch(Weapon)
	{
	case -1: return !HookHitDisabled && Tune.m_PlayerHooking != 0;
	case WEAPON_HAMMER: return !HammerHitDisabled;
	case WEAPON_SHOTGUN: return !ShotgunHitDisabled;
	case WEAPON_LASER: return !LaserHitDisabled;
	default: return !GrenadeHitDisabled;
	}
}

std::optional<vec2> BounceAim(CGameWorld &World, const CCharacter *pSelf, int SelfId, vec2 Origin, int TargetId, vec2 Target, float Radius, const CTuningParams &Tune, float Reach)
{
	// 0x1402217d0:768-773: the wallbounce trace starts with the reach of the weapon
	// in hand (FUN_140221200), not with the laser reach.
	auto *pCollision = World.Collision();
	const int Limit = std::clamp(static_cast<int>(static_cast<float>(Tune.m_LaserBounceNum)), 0, 8);
	const int Rays = std::clamp(g_Config.m_NullAimbotWallbounceRays, 24, 360);
	std::optional<vec2> Best;
	int BestBounces = Limit + 1;
	float BestDistance = Radius;
	for(int Ray = 0; Limit > 0 && Ray < Rays; ++Ray)
	{
		const vec2 Initial = WireDirection(direction(Ray * (2.0f * pi) / Rays));
		vec2 Position = Origin, Direction = WireDirection(Initial);
		float Remaining = Reach;
		for(int Bounce = 0; Bounce <= Limit && Remaining > 0; ++Bounce)
		{
			vec2 Stop = Position + Direction * Remaining;
			const bool Wall = pCollision->IntersectLineTeleWeapon(Position, Stop, nullptr, &Stop) != 0;
			vec2 Closest;
			const auto *pHit = World.IntersectCharacter(Position, Stop, 0, Closest, Bounce == 0 ? pSelf : nullptr, SelfId);
			if(pHit && pHit->GetId() != TargetId)
				break;
			if(Bounce)
			{
				if(!closest_point_on_line(Position, Stop, Target, Closest))
					Closest = Position;
				const float Distance = distance(Target, Closest);
				if(Distance <= Radius && (Bounce < BestBounces || (Bounce == BestBounces && Distance < BestDistance)))
				{
					Best = Initial;
					BestBounces = Bounce;
					BestDistance = Distance;
					break;
				}
			}
			if(!Wall)
				break;
			vec2 Reflection = Direction * 4.0f;
			pCollision->MovePoint(&Stop, &Reflection, 1.0f, nullptr);
			const float Travel = distance(Position, Stop);
			if(length(Reflection) < .001f || Travel < .001f)
				break;
			Remaining -= Travel + static_cast<float>(Tune.m_LaserBounceCost);
			Position = Stop;
			Direction = normalize(Reflection);
		}
	}
	return Best;
}

} // namespace

bool CAimbot::DebugMessage(char *pBuffer, int Size) const
{
	switch(m_State.m_Status)
	{
	case EStatus::CLEAR: return false;
	case EStatus::AVOID: str_copy(pBuffer, Localize("aimbot: god mode has the aim"), Size); break;
	case EStatus::INACTIVE_WEAPON: str_copy(pBuffer, Localize("aimbot: this weapon is not ticked"), Size); break;
	case EStatus::BUSY: str_copy(pBuffer, Localize("aimbot: the aim is in use by something unticked"), Size); break;
	case EStatus::NOBODY:
		// 0x1402208a0 clears the walled counter but never increments it.
		str_format(pBuffer, Size, Localize("aimbot: nobody  (%d seen, %d far, %d behind, %d walled, %d no shot)"),
			m_State.m_Seen, m_State.m_Far, m_State.m_Behind, 0, m_State.m_NoShot);
		break;
	}
	return true;
}

CNetObj_PlayerInput CAimbot::Apply(CGameClient *pClient, CNetObj_PlayerInput Input, int Slot, bool AvoidChanged)
{
	if(!NullProfileActive() || Slot < 0 || Slot > 1)
		return Input;
	auto &State = m_State;
	const int PreviousFire = State.m_LastFire;
	const bool NewHook = Input.m_Hook && !State.m_LastHook;
	State.m_LastHook = Input.m_Hook;
	State.m_LastFire = Input.m_Fire;
	State.m_Target = State.m_Weapon = -1;
	const bool HoldHook = g_Config.m_NullBindAimbotHook && pClient->Input()->KeyIsPressed(g_Config.m_NullBindAimbotHook);
	if(pClient->Client()->State() != IClient::STATE_ONLINE)
		return Input;
	if(!g_Config.m_NullAimbot && !HoldHook)
	{
		State.m_Status = EStatus::INACTIVE_WEAPON;
		return Input;
	}
	if(AvoidChanged)
	{
		State.m_Status = EStatus::AVOID;
		return Input;
	}
	auto &World = pClient->KrxPredictedWorld();
	const int Id = pClient->m_aLocalIds[Slot];
	if(Id < 0 || Id >= MAX_CLIENTS || !pClient->m_Snap.m_aCharacters[Id].m_Active)
		return Input;
	const auto &Snapshot = pClient->m_Snap.m_aCharacters[Id].m_Cur;
	auto *pSelf = World.GetCharacterById(Id);
	// 0x14021ffc0:254-263: without a local character entity the original aims from the
	// snapshot position with the fixed 28-unit radius instead of bailing out.
	const vec2 SelfPosition = pSelf ? pSelf->Core()->m_Pos : pClient->m_aClients[Id].m_RenderPos;
	const float SelfRadius = pSelf ? pSelf->GetProximityRadius() : 28.0f;
	const bool Firing = (Input.m_Fire & 1) || CountInput(PreviousFire, Input.m_Fire).m_Presses > 0;
	const int ActiveWeapon = pSelf ? pSelf->GetActiveWeapon() : Snapshot.m_Weapon;
	const bool AimWeapon = Firing && WeaponSettings(ActiveWeapon).m_Enabled;
	const int HookState = pSelf ? pSelf->Core()->m_HookState : Snapshot.m_HookState;
	if(HoldHook && pSelf && HookState != HOOK_IDLE)
	{
		const int HookedPlayer = pSelf ? pSelf->Core()->HookedPlayer() : Snapshot.m_HookedPlayer;
		const bool AttachedToPlayer = HookState == HOOK_GRABBED && HookedPlayer >= 0;
		Input.m_Hook = AttachedToPlayer || HookState == HOOK_FLYING;
		if(!AttachedToPlayer || !AimWeapon)
		{
			if(AttachedToPlayer)
			{
				State.m_Status = EStatus::CLEAR;
				State.m_Target = HookedPlayer;
				State.m_Weapon = -1;
			}
			return Input;
		}
	}
	const bool CanStartHook = HookState == HOOK_IDLE || NewHook;
	const bool Hook = CanStartHook && (HoldHook || (Input.m_Hook && g_Config.m_NullAimbotHook));
	if(!HoldHook && CanStartHook && Input.m_Hook && !g_Config.m_NullAimbotHook)
	{
		State.m_Status = EStatus::BUSY;
		return Input;
	}
	if(!Hook && !AimWeapon)
	{
		State.m_Status = Firing ? EStatus::BUSY : EStatus::INACTIVE_WEAPON;
		return Input;
	}
	State.m_Seen = State.m_Far = State.m_Behind = State.m_NoShot = 0;
	const int Weapon = Hook ? -1 : ActiveWeapon;
	const auto Settings = WeaponSettings(Weapon);
	const auto &Tune = pSelf ? *pSelf->GetTuning(pSelf->GetPureTuneZone()) : *pClient->GetTuning(0);
	const bool DDRace = World.m_WorldConfig.m_IsDDRace;
	const bool Projectile = Weapon == WEAPON_GUN || Weapon == WEAPON_GRENADE || (Weapon == WEAPON_SHOTGUN && !DDRace);
	const float Curvature = Weapon == WEAPON_GUN ? Tune.m_GunCurvature : Weapon == WEAPON_GRENADE ? Tune.m_GrenadeCurvature : Tune.m_ShotgunCurvature;
	const float Speed = Weapon == WEAPON_GUN ? Tune.m_GunSpeed : Weapon == WEAPON_GRENADE ? Tune.m_GrenadeSpeed : Tune.m_ShotgunSpeed;
	const float Range = Reach(Tune, Weapon, SelfRadius, DDRace);
	const float Fov = std::cos((std::min(180.0f, Settings.m_Fov * .5f) * 3.1415927f) / 180.0f);
	vec2 Mouse = pClient->m_Controls.m_aMousePos[Slot];
	Mouse = length(Mouse) < .001f ? vec2(1, 0) : normalize(Mouse);
	const vec2 Position = SelfPosition;
	if(State.m_StickyWeapon != Weapon)
		State.m_StickyTarget = -1;
	State.m_StickyWeapon = Weapon;

	struct CCandidate
	{
		int m_Id;
		float m_Score;
	};
	std::array<CCandidate, MAX_CLIENTS> aCandidates;
	int NumCandidates = 0;
	struct CTraceTee
	{
		int m_Id;
		float m_Radius;
		vec2 m_Position;
		vec2 m_Velocity;
		bool m_Extrapolate;
		unsigned m_ExtrapolatedTicks = 0;
		std::array<vec2, 5> m_aExtrapolated;
	};
	std::array<CTraceTee, MAX_CLIENTS> aTraceTees;
	int NumTraceTees = 0;
	for(int Other = 0; Other < MAX_CLIENTS; ++Other)
	{
		if(!CanTarget(pClient, World, pSelf, Id, Other, Weapon, Tune))
			continue;
		const auto *pOther = World.GetCharacterById(Other);
		const float TargetRadius = pOther ? pOther->GetProximityRadius() : 28.0f;
		if(Weapon != WEAPON_HAMMER)
		{
			auto &Tee = aTraceTees[NumTraceTees++];
			Tee.m_Id = Other;
			Tee.m_Radius = TargetRadius;
			Tee.m_Position = TargetPosition(pClient, World, Other);
			Tee.m_Extrapolate = pOther && Projectile && g_Config.m_NullAimbotLead;
			if(Tee.m_Extrapolate)
				Tee.m_Velocity = pOther->Core()->m_Vel;
		}
		if(g_Config.m_NullAimbotIgnoreFriends && pClient->m_aClients[Other].m_Friend)
			continue;
		++State.m_Seen;
		const float Margin = Hook ? SelfRadius + 2.0f : TargetRadius;
		const vec2 Delta = TargetPosition(pClient, World, Other) - Position;
		const float Distance = length(Delta);
		if(Distance < .001f || Range < Distance - Margin)
		{
			++State.m_Far;
			continue;
		}
		const float Facing = dot(Mouse, Delta / Distance);
		if(Facing < Fov && (!Hook || std::acos(std::clamp(Facing, -1.0f, 1.0f)) >
			std::acos(std::clamp(Fov, -1.0f, 1.0f)) + std::asin(std::min(1.0f, Margin / Distance))))
		{
			++State.m_Behind;
			continue;
		}
		float Score = Settings.m_Priority == 1 ? Distance : 1.0f - Facing;
		if(g_Config.m_NullAimbotSticky && Other == State.m_StickyTarget)
			Score *= 1.0f - g_Config.m_NullAimbotStickyMargin / 100.0f;
		aCandidates[NumCandidates++] = {Other, Score};
	}
	std::sort(aCandidates.begin(), aCandidates.begin() + NumCandidates, [](const CCandidate &A, const CCandidate &B) {
		return A.m_Score < B.m_Score || (A.m_Score == B.m_Score && A.m_Id < B.m_Id);
	});
	const float Ping = PingLead(pClient);
	const float HookSpeed = std::max(1.0f, static_cast<float>(Tune.m_HookFireSpeed));
	const float Lifetime = Weapon == WEAPON_GUN ? Tune.m_GunLifetime : Weapon == WEAPON_GRENADE ? Tune.m_GrenadeLifetime : Tune.m_ShotgunLifetime;
	const int Horizon = !g_Config.m_NullAimbotLead && Ping <= 0 ? 0 :
		Projectile ? 100 : Hook ? std::clamp(static_cast<int>(std::ceil(Range / HookSpeed)) + 4, 1, 100) :
		// 0x14039f420 is ceilf on both the hook and the ping branch.
		std::clamp(static_cast<int>(std::ceil(Ping)), 0, 100);

	const float FireSpeed = Tune.m_HookFireSpeed;
	const int HookTicks = Hook && Range > 0 && FireSpeed > 0 ?
		std::clamp(static_cast<int>(std::ceil(Range / HookSpeed)) + 4, 1, 100) : 0;
	const auto OriginPath = TargetPath(pClient, World, Id, HookTicks, Tune, true);

	const auto Solve = [&](int Target, float Radius, const CTargetPath &Path) -> std::optional<vec2> {
		const auto PointAt = [&](float Ticks) { return Path.At(Ping + (g_Config.m_NullAimbotLead ? Ticks : 0)); };
		const auto FirstHit = [&](vec2 Start, vec2 End, int Step) {
			int Hit = -1;
			float Best = 0.0f;
			for(int Index = 0; Index < NumTraceTees; ++Index)
			{
				auto &Tee = aTraceTees[Index];
				vec2 Center = Tee.m_Position;
				if(Tee.m_Id == Target)
					Center = PointAt(static_cast<float>(Step));
				else if(Tee.m_Extrapolate)
				{
					// 0x14021c370 extrapolates projectile blockers at most four ticks.
					const int Lead = std::clamp(Step, 0, 4);
					if(!(Tee.m_ExtrapolatedTicks & (1U << Lead)))
					{
						vec2 Movement = Tee.m_Velocity * static_cast<float>(Lead);
						World.Collision()->MoveBox(&Center, &Movement, vec2(28, 28), vec2(0, 0));
						Tee.m_aExtrapolated[Lead] = Center;
						Tee.m_ExtrapolatedTicks |= 1U << Lead;
					}
					else
						Center = Tee.m_aExtrapolated[Lead];
				}
				vec2 Closest;
				if(!closest_point_on_line(Start, End, Center, Closest))
					Closest = Start;
				if(distance(Center, Closest) >= (Hook ? SelfRadius + 2.0f : Tee.m_Radius + (Projectile ? 6.0f : 0.0f)))
					continue;
				const float Along = distance(Start, Hook ? Center : Closest);
				if(Hit < 0 || Along < Best)
				{
					Hit = Tee.m_Id;
					Best = Along;
				}
			}
			return Hit;
		};
		if(Projectile)
		{
			vec2 Aim = normalize(Path.m_aPos[0] - Position);
			float Estimate = 0;
			for(int Pass = 0; Pass < 6; ++Pass)
			{
				const vec2 Start = Position + Aim * SelfRadius * .75f;
				const vec2 Point = PointAt(Estimate);
				std::array<CBallisticAim, 2> Shots;
				int Count = SolveBallistic(Start, Point, Curvature, Speed, 50, Shots);
				if(!Count && Weapon == WEAPON_GRENADE && distance(Start, Point) > 70)
					Count = SolveBallistic(Start, Point - normalize(Point - Start) * 70.0f, Curvature, Speed, 50, Shots);
				bool Found = false;
				for(int Shot = 0; Shot < Count; ++Shot)
				{
					const auto &Solution = Shots[Shot];
					if(Solution.m_Ticks <= 0 || Solution.m_Ticks > std::min(100.0f, Lifetime * 50) ||
						(Shot > 0 && !g_Config.m_NullAimbotArc))
						continue;
					vec2 Previous = Start;
					const vec2 Ray = WireDirection(Solution.m_Direction);
					bool Clear = true;
					for(int Tick = 1; Tick <= static_cast<int>(std::ceil(Solution.m_Ticks)); ++Tick)
					{
						const vec2 Next = CalcPos(Start, Ray, Curvature, Speed, Tick / 50.0f);
						vec2 Hit, Closest;
						if(World.Collision()->IntersectLine(Previous, Next, &Hit, nullptr))
						{
							Clear = (closest_point_on_line(Previous, Hit, Point, Closest) && distance(Closest, Point) < Radius + 6) ||
								(Weapon == WEAPON_GRENADE && distance(Hit, Point) < 70);
							break;
						}
						Previous = Next;
					}
					if(Clear)
					{
						Aim = Ray;
						Estimate = Solution.m_Ticks;
						Found = true;
						break;
					}
				}
				if(!Found)
					return {};
			}
			const auto HitsTarget = [&](vec2 Ray) {
				const vec2 Start = Position + Ray * SelfRadius * .75f;
				vec2 Prior = Start;
				for(int Step = 1; Step <= static_cast<int>(std::ceil(Estimate)); ++Step)
				{
					const vec2 Next = CalcPos(Start, Ray, Curvature, Speed, Step / 50.0f);
					vec2 Impact;
					const bool HitWall = World.Collision()->IntersectLine(Prior, Next, &Impact, nullptr) != 0;
					const vec2 End = HitWall ? Impact : Next;
					const int Hit = FirstHit(Prior, End, Step);
					if(Hit >= 0)
						return Hit == Target;
					if(HitWall)
						return Weapon == WEAPON_GRENADE && distance(Impact, PointAt(Step)) < 70.0f;
					Prior = Next;
				}
				return Weapon == WEAPON_GRENADE && distance(Prior, PointAt(Estimate)) < 70.0f;
			};
			if(!HitsTarget(Aim))
				return {};
			const vec2 Delta = PointAt(Estimate) - Position;
			const float Distance = length(Delta);
			if(Settings.m_Knock && Distance > .001f && Distance <= g_Config.m_NullAimbotKnockRange)
			{
				const vec2 Direct = WireDirection(Delta / Distance);
				if(HitsTarget(Direct))
					return Direct;
			}
			return Aim;
		}
		vec2 Point = PointAt(0);
		if(Hook)
		{
			for(int Pass = 0; Pass < 6; ++Pass)
				Point = PointAt(std::max(0.0f, distance(Point, Position) - SelfRadius * 1.5f) / HookSpeed);
		}
		const vec2 Delta = Point - Position;
		const float Distance = length(Delta);
		if(Distance < .001f)
			return {};
		const float Center = angle(Delta);
		std::vector<float> vAngles;
		if(Hook)
			vAngles = HookAngles(*World.Collision(), Position, Point, SelfRadius, Mouse, Fov);
		else
		{
			// 0x1402217d0:632-753 uses a 33-ray fan for hammer and line weapons.
			const float Width = std::asin(std::min(.95f, Radius * .95f / Distance));
			vAngles.reserve(33);
			for(int Sample = 0; Sample < 33; ++Sample)
				vAngles.push_back(Center + (Sample % 2 ? 1 : -1) * ((Sample + 1) / 2) * (Width / 16));
		}
		const float HookConeSine = std::min(.95f, (SelfRadius + 1.5f) / Distance);
		const float HookCone = std::sqrt(1.0f - HookConeSine * HookConeSine);
		for(float Angle : vAngles)
		{
			const vec2 Ray = WireDirection(direction(Angle));
			if(Hook && (dot(Mouse, Ray) < Fov || dot(Delta / Distance, Ray) < HookCone))
				continue;
			if(Weapon == WEAPON_HAMMER)
			{
				const vec2 Origin = Position + Ray * SelfRadius * .75f;
				if(distance(Point, Origin) < SelfRadius * .5f + Radius && !World.Collision()->IntersectLine(Origin, Point, nullptr, nullptr))
					return Ray;
				continue;
			}
			if(Hook)
			{
				vec2 Tip = Position + Ray * SelfRadius * 1.5f;
				for(int Step = 0; Step < HookTicks; ++Step)
				{
					const vec2 Base = OriginPath.At(static_cast<float>(Step));
					vec2 End = Tip + Ray * FireSpeed;
					const bool AtLimit = distance(Base, End) > Range;
					if(AtLimit)
						End = Base + normalize(End - Base) * Range;
					const bool HitTile = World.Collision()->IntersectLineTeleHook(Tip, End, &End, nullptr) != 0;
					const int Hit = FirstHit(Tip, End, Step);
					if(Hit == Target)
						return Ray;
					if(Hit >= 0 || HitTile || AtLimit)
						break;
					Tip = End;
				}
			}
			else
			{
				vec2 Stop = Position + Ray * Range;
				World.Collision()->IntersectLineTeleWeapon(Position, Stop, nullptr, &Stop);
				if(FirstHit(Position, Stop, 0) == Target)
					return Ray;
			}
		}
		if(!Hook && Weapon != WEAPON_HAMMER && g_Config.m_NullAimbotWallbounce)
			return BounceAim(World, pSelf, Id, Position, Target, Point, Radius, Tune, Range);
		return {};
	};
	int Target = -1;
	vec2 Aim;
	for(int Index = 0; Index < std::min(6, NumCandidates); ++Index)
	{
		const int Other = aCandidates[Index].m_Id;
		const auto *pOther = World.GetCharacterById(Other);
		const float Radius = pOther ? pOther->GetProximityRadius() : 28.0f;
		const auto Path = TargetPath(pClient, World, Other, Horizon, Tune);
		const auto Result = Solve(Other, Radius, Path);
		if(Result && dot(Mouse, *Result) >= Fov)
		{
			Target = Other;
			Aim = *Result;
			break;
		}
		++State.m_NoShot;
	}
	State.m_Status = Target >= 0 ? EStatus::CLEAR : EStatus::NOBODY;
	State.m_StickyTarget = State.m_Target = Target;
	if(Target < 0)
		return Input;
	State.m_Weapon = Weapon;
	Input.m_TargetX = round_to_int(Aim.x * 10000.0f);
	Input.m_TargetY = round_to_int(Aim.y * 10000.0f);
	if(HoldHook && Weapon == -1)
		Input.m_Hook = 1;
	return Input;
}
}
