#include "helpers.h"

#include <engine/client.h>
#include <engine/shared/client_profile.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <base/math.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

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
				AimAt(Input, pTarget->Core()->m_Pos - pCore->m_Pos);
				Fire(Input);
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
