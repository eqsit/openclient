#include "helpers.h"
#include "simulation.h"

#include <engine/client.h>
#include <engine/shared/client_profile.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/mapitems.h>
#include <base/math.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace OpenKrx::NullClient
{
namespace
{
void AimAt(CNetObj_PlayerInput &Input, vec2 Difference)
{
	const float Length = length(Difference);
	if(Length > 0.001f)
	{
		const vec2 Aim = Difference * (1.0f / Length) * std::max(100.0f, Length);
		Input.m_TargetX = static_cast<int>(Aim.x);
		Input.m_TargetY = static_cast<int>(Aim.y);
	}
}
void Fire(CNetObj_PlayerInput &Input)
{
	Input.m_Fire = static_cast<int>(static_cast<unsigned>(Input.m_Fire) + 2U);
}
bool Held(CGameClient *pClient, int Key)
{
	return Key == 0 || pClient->Input()->KeyIsPressed(Key);
}

bool SafeGrenade(const CGameWorld &Source, int Id, vec2 Direction)
{
	if(!g_Config.m_NullAutoGrenadeSafe)
		return true;
	CSettings Settings;
	Settings.m_Horizon = 24;
	Settings.m_Weapons = true;
	Settings.m_Adversarial = 0;
	CSimulation Simulation;
	if(!Simulation.Initialize(Source, Id, Settings, {}))
		return true;

	auto *pCharacter = Simulation.Character();
	pCharacter->GiveWeapon(WEAPON_GRENADE);
	pCharacter->SetWeaponAmmo(WEAPON_GRENADE, -1);
	pCharacter->SetWeapon(WEAPON_GRENADE);
	CNetObj_PlayerInput Input{};
	Input.m_PlayerFlags = PLAYERFLAG_PLAYING;
	Input.m_WantedWeapon = WEAPON_GRENADE + 1;
	Input.m_TargetX = static_cast<int>(Direction.x * 200.0f);
	Input.m_TargetY = static_cast<int>(Direction.y * 200.0f);
	if(!Input.m_TargetX && !Input.m_TargetY)
		Input.m_TargetX = 1;
	for(int Tick = 1; Tick <= 24; ++Tick)
	{
		Input.m_Fire = Tick;
		if(!Simulation.Tick(Input) || Simulation.Dead())
			return false;
		const auto *pLocal = Simulation.Character();
		if(pLocal->m_FreezeTime > 0 || pLocal->Core()->m_DeepFrozen)
			return false;
	}
	return true;
}

std::optional<vec2> FindDummyLaser(CGameWorld &World, CCharacter &Shooter, const CCharacter &Target)
{
	const vec2 Delta = Target.Core()->m_Pos - Shooter.Core()->m_Pos;
	const float Distance = length(Delta);
	if(Distance < .001f)
		return std::nullopt;
	const auto &Tuning = *Shooter.GetTuning(Shooter.GetPureTuneZone());
	const int MaxBounces = std::clamp(static_cast<int>(static_cast<float>(Tuning.m_LaserBounceNum)), 0, 8);
	auto *pCollision = World.Collision();
	const auto Trace = [&](vec2 Direction) {
		vec2 Start = Shooter.Core()->m_Pos;
		float Energy = Tuning.m_LaserReach;
		for(int Bounce = 0; Bounce <= MaxBounces && Energy >= 0; ++Bounce)
		{
			vec2 End = Start + Direction * Energy;
			const int Collision = pCollision->IntersectLineTeleWeapon(Start, End, nullptr, &End);
			vec2 Intersection;
			const bool SkipShooter = Bounce == 0 || World.m_WorldConfig.m_OldLaser || !World.m_WorldConfig.m_IsDDRace;
			if(const auto *pHit = World.IntersectCharacter(Start, End, 0.0f, Intersection, SkipShooter ? &Shooter : nullptr, Shooter.GetId()))
				return pHit == &Target ? Bounce : -1;
			if(Bounce == MaxBounces || Collision <= 0 || Collision == TILE_TELEINWEAPON)
				break;
			vec2 Reflection = Direction * 4.0f;
			pCollision->MovePoint(&End, &Reflection, 1.0f, nullptr);
			const float Travel = distance(Start, End);
			if(Travel < .001f || length(Reflection) < .001f)
				break;
			Energy -= Travel + static_cast<float>(Tuning.m_LaserBounceCost);
			Start = End;
			Direction = normalize(Reflection);
		}
		return -1;
	};
	const auto Aim = [](float Angle) {
		const vec2 Wire(round_to_int(std::cos(Angle) * 10000.0f), round_to_int(std::sin(Angle) * 10000.0f));
		return normalize(Wire);
	};
	const float Center = std::atan2(Delta.y, Delta.x);
	const float HalfWidth = std::asin(std::min(.95f, Target.GetProximityRadius() * .9f / Distance));
	for(int Sample = 0; Sample < 33; ++Sample)
	{
		const auto Direction = Aim(Center - HalfWidth + 2.0f * HalfWidth * Sample / 32);
		if(Trace(Direction) == 0)
			return Direction;
	}
	if(MaxBounces == 0)
		return std::nullopt;
	std::optional<vec2> Best;
	int BestBounces = MaxBounces + 1;
	for(int Angle = 0; Angle < 360; ++Angle)
	{
		const auto Direction = Aim(Angle * (6.28318530718f / 360.f));
		const int Bounces = Trace(Direction);
		if(Bounces < 0 || Bounces >= BestBounces)
			continue;
		Best = Direction;
		BestBounces = Bounces;
		if(Bounces <= 1)
			break;
	}
	return Best;
}
}

CNetObj_PlayerInput CHelpers::Apply(CGameClient *pClient, CNetObj_PlayerInput Input, int Slot, bool AvoidChanged, bool AvoidFire, bool AvoidHookAim)
{
	if(!NullProfileActive() || Slot < 0 || Slot > 1 || pClient->Client()->State() != IClient::STATE_ONLINE)
		return Input;
	auto &State = m_aSlots[Slot];
	auto &World = pClient->KrxPredictedWorld();
	const int Id = pClient->m_aLocalIds[Slot];
	auto *pCharacter = World.GetCharacterById(Id);
	if(!pCharacter)
	{
		State = {};
		return Input;
	}
	const int Tick = pClient->Client()->GameTick(Slot);
	const auto *pCore = pCharacter->Core();
	const int Weapon = pCharacter->GetActiveWeapon();
	const bool Frozen = pCharacter->m_FreezeTime != 0;
	const bool Ready = pCharacter->DiagnosticReloadTimer() == 0 && !Frozen;
	const int OriginalFire = Input.m_Fire;
	std::array<std::optional<vec2>, MAX_CLIENTS> aTargets;
	for(int Other = 0; Other < MAX_CLIENTS; ++Other)
		if(const auto *pOther = World.GetCharacterById(Other))
			aTargets[Other] = pOther->Core()->m_Pos;
	State.m_Automation.Apply(Input, pCore->HookedPlayer(), pCore->m_Pos, aTargets,
		g_Config.m_NullAutoFire, g_Config.m_NullAutoRehook, g_Config.m_NullAutoRehookTicks,
		Ready, pCharacter->GetWeaponAmmo(Weapon) != 0, AvoidChanged, AvoidFire);

	// 140211b60: hold-key balance uses all active tees, nearest within 2000.
	if(g_Config.m_NullBindBalanceBot && Held(pClient, g_Config.m_NullBindBalanceBot) && !AvoidChanged)
	{
		float Best = 2000.0f;
		std::optional<float> TargetX;
		for(int Other = 0; Other < MAX_CLIENTS; ++Other)
		{
			if(Other == Id || !pClient->m_Snap.m_aCharacters[Other].m_Active)
				continue;
			const auto *pTarget = World.GetCharacterById(Other);
			const vec2 Position = pTarget ? pTarget->Core()->m_Pos : pClient->m_aClients[Other].m_Predicted.m_Pos;
			const float Distance = distance(Position, pCore->m_Pos);
			if(Distance < Best)
				Best = Distance, TargetX = Position.x;
		}
		if(TargetX)
			Input.m_Direction = std::abs(*TargetX - pCore->m_Pos.x) <= 10.0f ? 0 : *TargetX > pCore->m_Pos.x ? 1 : -1;
	}
	// 140212250: choose braking only when it beats the natural friction.
	if(g_Config.m_NullQuickStop && !AvoidChanged && Input.m_Direction == 0 && !Frozen && !pCore->m_DeepFrozen && std::abs(pCore->m_Vel.x) > 1.0f)
	{
		const bool Grounded = pCharacter->IsGrounded();
		if(Grounded || !g_Config.m_NullQuickStopGround)
		{
			const auto *pTune = pCharacter->GetTuning(pCharacter->GetPureTuneZone());
			const float Speed = Grounded ? pTune->m_GroundControlSpeed : pTune->m_AirControlSpeed;
			const float Accel = Grounded ? pTune->m_GroundControlAccel : pTune->m_AirControlAccel;
			const float Friction = Grounded ? pTune->m_GroundFriction : pTune->m_AirFriction;
			const float Brake = SaturatedAdd(-Speed, Speed, pCore->m_Vel.x, pCore->m_Vel.x > 0 ? -Accel : Accel);
			if(std::abs(Brake) < std::abs(Friction * pCore->m_Vel.x))
				Input.m_Direction = pCore->m_Vel.x > 0 ? -1 : 1;
		}
	}
	if(g_Config.m_NullAutoHammer && Held(pClient, g_Config.m_NullBindAutoHammer) && !AvoidChanged && !AvoidFire && Weapon == WEAPON_HAMMER && Ready && pCharacter->GetWeaponAmmo(Weapon) != 0)
	{
		const float Scale = g_Config.m_NullAutoHammerRange == 1 ? 2.2f : g_Config.m_NullAutoHammerRange == 2 ? 3.5f : 1.25f;
		float Best = Scale * pCharacter->GetProximityRadius();
		int Target = -1;
		for(int Other = 0; Other < MAX_CLIENTS; ++Other)
		{
			if(Other == Id || !pClient->m_Snap.m_aCharacters[Other].m_Active || !pCharacter->CanCollide(Other))
				continue;
			const auto *pOther = World.GetCharacterById(Other);
			const vec2 Position = pOther ? pOther->Core()->m_Pos : pClient->m_aClients[Other].m_Predicted.m_Pos;
			const float Distance = distance(Position, pCore->m_Pos);
			if(Distance < Best)
				Best = Distance, Target = Other;
		}
		if(Target >= 0)
		{
			const auto *pTarget = World.GetCharacterById(Target);
			AimAt(Input, (pTarget ? pTarget->Core()->m_Pos : pClient->m_aClients[Target].m_Predicted.m_Pos) - pCore->m_Pos);
			Fire(Input);
		}
	}
	if(g_Config.m_NullCounterHammer && !AvoidChanged && !AvoidFire && Input.m_Fire == OriginalFire &&
		Weapon == WEAPON_HAMMER && Ready && !pCore->m_DeepFrozen && pCharacter->GetWeaponAmmo(Weapon) != 0)
	{
		const CCharacter *pAttacker = nullptr;
		float Nearest = std::numeric_limits<float>::infinity();
		for(int Other = 0; Other < MAX_CLIENTS; ++Other)
		{
			if(Other == Id || !pClient->m_Snap.m_aCharacters[Other].m_Active || !pCharacter->CanCollide(Other))
				continue;
			const auto *pOther = World.GetCharacterById(Other);
			if(!pOther || pOther->GetActiveWeapon() != WEAPON_HAMMER ||
				pOther->GetAttackTick() > Tick || Tick - pOther->GetAttackTick() >= 4)
				continue;
			const float Distance = distance(pCore->m_Pos, pOther->Core()->m_Pos);
			if(Distance <= pCharacter->GetProximityRadius() * 1.25f + pOther->GetProximityRadius() && Distance < Nearest)
			{
				pAttacker = pOther;
				Nearest = Distance;
			}
		}
		if(pAttacker)
		{
			AimAt(Input, pAttacker->Core()->m_Pos - pCore->m_Pos);
			Fire(Input);
		}
	}
	if(!g_Config.m_NullAutoGrenade || !Held(pClient, g_Config.m_NullBindAutoGrenade))
		State.m_GrenadeDelay = -1, State.m_HasVelocity = false;
	else
	{
		if(State.m_GrenadeTick != Tick)
		{
			if(State.m_HasVelocity && Tick == State.m_GrenadeTick + 1)
			{
				const vec2 Shock = pCore->m_Vel - State.m_Velocity;
				const float ShockLength = length(Shock);
				if(ShockLength > g_Config.m_NullAutoGrenadeShock)
				{
					float Best = g_Config.m_NullAutoGrenadeRange * 32.0f;
					int Target = -1;
					for(int Other = 0; Other < MAX_CLIENTS; ++Other)
					{
						auto *pOther = World.GetCharacterById(Other);
						if(Other == Id || !pClient->m_Snap.m_aCharacters[Other].m_Active || !pOther || !pCharacter->CanCollide(Other) || pOther->GetActiveWeapon() != WEAPON_HAMMER || Tick - pOther->GetAttackTick() >= 4 || pOther->GetAttackTick() > Tick)
							continue;
						const vec2 Delta = pCore->m_Pos - pOther->Core()->m_Pos;
						const float Distance = length(Delta);
						if(Distance >= 0.001f && dot(Delta * (1.0f / Distance), Shock * (1.0f / ShockLength)) >= 0.3f && Distance < Best)
							Best = Distance, Target = Other;
					}
					if(Target >= 0)
						State.m_GrenadeTarget = Target, State.m_GrenadeDelay = std::max(0, g_Config.m_NullAutoGrenadeDelay);
				}
			}
			State.m_GrenadeTick = Tick;
			State.m_Velocity = pCore->m_Vel;
			State.m_HasVelocity = true;
			if(State.m_GrenadeDelay > 0)
				--State.m_GrenadeDelay;
		}
		if(!AvoidHookAim && !AvoidFire && State.m_GrenadeDelay == 0 && Weapon == WEAPON_GRENADE && Ready)
		{
			const auto *pTarget = World.GetCharacterById(State.m_GrenadeTarget);
			if(pTarget && distance(pTarget->Core()->m_Pos, pCore->m_Pos) <= g_Config.m_NullAutoGrenadeRange * 32.0f * 1.5f)
			{
				const vec2 Delta = pTarget->Core()->m_Pos - pCore->m_Pos;
				const float Distance = length(Delta);
				if(Distance > .001f && SafeGrenade(World, Id, Delta / Distance))
				{
					AimAt(Input, Delta);
					Fire(Input);
				}
			}
			State.m_GrenadeDelay = State.m_GrenadeTarget = -1;
		}
	}
	// 1402131d0 / 140212c60: fake aim never replaces a real hook or shot.
	const int PreviousFire = State.m_FakeFire;
	State.m_FakeFire = Input.m_Fire;
	State.m_HasFakeAim = false;
	if(g_Config.m_NullFakeAim)
	{
		if(Tick != State.m_FakeTick)
		{
			if(g_Config.m_NullFakeAimMode == 3)
				State.m_FakeAngle = static_cast<float>(std::rand() % 3600) * 6.2831855f / 3600.0f;
			else if(State.m_FakeTick >= 0)
				State.m_FakeAngle = std::fmod(State.m_FakeAngle + ((g_Config.m_NullFakeAimSpin * 3.1415927f / 180.0f) / 50.0f) * std::clamp(Tick - State.m_FakeTick, 0, 50), 6.2831855f);
			State.m_FakeTick = Tick;
		}
		const vec2 Real(Input.m_TargetX, Input.m_TargetY);
		if(AvoidFire || Input.m_Hook || Input.m_Fire != PreviousFire || (Input.m_Fire & 1))
		{
			if(length(Real) > 0.001f)
				State.m_RealAim = normalize(Real);
		}
		else
		{
			vec2 Aim = pClient->m_Controls.m_aMousePos[Slot];
			Aim = length(Aim) < 0.001f ? vec2(1, 0) : normalize(Aim);
			switch(g_Config.m_NullFakeAimMode)
			{
			case 1: Aim = State.m_RealAim; break;
			case 2: case 3: Aim = vec2(std::cos(State.m_FakeAngle), std::sin(State.m_FakeAngle)); break;
			case 4: Aim = -Aim; break;
			case 5:
				{
					float Best = -1;
					for(int Other = 0; Other < MAX_CLIENTS; ++Other)
					{
						const auto *pOther = World.GetCharacterById(Other);
						if(Other == Id || !pOther || !pClient->m_Snap.m_aCharacters[Other].m_Active)
							continue;
						const vec2 Delta = pOther->Core()->m_Pos - pCore->m_Pos;
						const float Distance = length(Delta);
						if(Distance >= 0.001f && (Best < 0 || Distance < Best))
							Best = Distance, Aim = Delta * (1.0f / Distance);
					}
					break;
				}
			}
			const float Radius = length(Real) < 1.0f ? 140.0f : length(Real);
			Input.m_TargetX = static_cast<int>(Aim.x * Radius);
			Input.m_TargetY = static_cast<int>(Aim.y * Radius);
			State.m_SentAim = Aim;
			State.m_HasFakeAim = true;
		}
	}
	return Input;
}

CNetObj_PlayerInput CHelpers::ApplyDummyLaser(CGameClient *pClient, CNetObj_PlayerInput Input, int Slot)
{
	if(!g_Config.m_NullDummyLaserUnfreeze || Slot == g_Config.m_ClDummy ||
		!pClient->Client()->DummyConnected() || pClient->m_Snap.m_SpecInfo.m_Active)
		return Input;
	const int Shooter = pClient->m_aLocalIds[Slot];
	const int Target = pClient->m_aLocalIds[g_Config.m_ClDummy];
	auto &World = pClient->KrxPredictedWorld();
	auto *pShooter = World.GetCharacterById(Shooter);
	auto *pTarget = World.GetCharacterById(Target);
	if(!pShooter || !pTarget || pTarget->m_FreezeTime <= 0 || pTarget->Core()->m_DeepFrozen || pTarget->Core()->m_LiveFrozen ||
		pShooter->m_FreezeTime > 0 || pShooter->Core()->m_DeepFrozen || pShooter->Core()->m_LiveFrozen ||
		pShooter->LaserHitDisabled() || !pShooter->CanCollide(Target) ||
		!pShooter->GetWeaponGot(WEAPON_LASER) || pShooter->GetWeaponAmmo(WEAPON_LASER) == 0 || Input.m_Hook)
		return Input;
	const int Tile = World.Collision()->GetPureMapIndex(pTarget->Core()->m_Pos);
	if((World.Collision()->GetTileIndex(Tile) == TILE_FREEZE || World.Collision()->GetFrontTileIndex(Tile) == TILE_FREEZE) &&
		!pTarget->IsGrounded())
		return Input;
	auto &State = m_aSlots[Slot];
	const int Tick = pClient->Client()->GameTick(g_Config.m_ClDummy);
	if(State.m_LaserScanTick != Tick || State.m_LaserShooter != Shooter || State.m_LaserTarget != Target)
	{
		State.m_LaserScanTick = Tick;
		State.m_LaserShooter = Shooter;
		State.m_LaserTarget = Target;
		State.m_LaserAim = FindDummyLaser(World, *pShooter, *pTarget);
	}
	if(!State.m_LaserAim)
		return Input;
	Input.m_WantedWeapon = WEAPON_LASER + 1;
	Input.m_TargetX = round_to_int(State.m_LaserAim->x * 10000.0f);
	Input.m_TargetY = round_to_int(State.m_LaserAim->y * 10000.0f);
	if(pShooter->GetActiveWeapon() == WEAPON_LASER && pShooter->DiagnosticReloadTimer() == 0 && State.m_LaserFireTick != Tick)
	{
		Input.m_Fire = (Input.m_Fire + 3) & (INPUT_STATE_MASK & ~1);
		State.m_LaserFireTick = Tick;
	}
	return Input;
}

void CHelpers::Update(CGameClient *pClient)
{
	if(!NullProfileActive() || pClient->Client()->State() != IClient::STATE_ONLINE || pClient->m_Snap.m_SpecInfo.m_Active)
		return;
	const auto *pCharacter = pClient->KrxPredictedWorld().GetCharacterById(pClient->m_aLocalIds[g_Config.m_ClDummy]);
	if(!pCharacter)
		return;
	const bool Deep = pCharacter->Core()->m_DeepFrozen;
	if((Deep ? g_Config.m_NullKillOnDeepFreeze : (pCharacter->m_FreezeTime > 0 && g_Config.m_NullKillOnFreeze)))
	{
		const int Tick = pClient->Client()->GameTick(g_Config.m_ClDummy);
		if(Tick - m_LastKillTick > 49)
		{
			m_LastKillTick = Tick;
			pClient->SendKill();
		}
	}
}

bool CHelpers::VisibleAim(int Slot, vec2 &Aim) const
{
	if(!NullProfileActive() || !g_Config.m_NullFakeAim || !g_Config.m_NullFakeAimVisible || Slot < 0 || Slot > 1 || !m_aSlots[Slot].m_HasFakeAim)
		return false;
	Aim = m_aSlots[Slot].m_SentAim;
	return true;
}
}
