#include "serial_threats.h"

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
namespace OpenKrx::NullClient
{
	namespace
	{
		bool Hazard(CCollision *pCollision, vec2 P)
		{
			const int I = pCollision->GetPureMapIndex(P), A = pCollision->GetTileIndex(I), B = pCollision->GetFrontTileIndex(I);
			return A == TILE_FREEZE || B == TILE_FREEZE || A == TILE_DFREEZE || B == TILE_DFREEZE || A == TILE_DEATH || B == TILE_DEATH;
		}
		vec2 Unit(vec2 V) { return length(V) > .001f ? normalize(V) : vec2(0, 0); }
		float TimeWeight(int Tick, int Horizon) { return Tick < 0 || Horizon <= 0 ? 0.f : std::clamp(1.f - (float)Tick / Horizon, 0.f, 1.f); }
		float DistanceWeight(float D, float Range)
		{
			const float F = Range > 0 ? 1.f - std::clamp(D / Range, 0.f, 1.f) : 0.f;
			return F * F;
		}
	}
	void CSerialThreats::Build(CGameWorld &World, int Own, int Target, float Range, int Horizon, float Throw, const std::array<bool, MAX_CLIENTS> &Idle, const CGameClient *pClient)
	{
		m_vThreats.clear();
		m_pCollision = World.Collision();
		m_Horizon = std::clamp(Horizon, 0, 24);
		m_Range = Range;
		m_Throw = Throw;
		auto *pSelf = World.GetCharacterById(Own);
		if(!pSelf)
			return;
		const auto *pTune = pSelf->GetTuning(pSelf->GetPureTuneZone());
		m_HookLength = pTune->m_HookLength;
		const vec2 SelfPosition = pClient ? pClient->m_aClients[Own].m_RenderPos : pSelf->Core()->m_Pos;
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		{
			auto *p = World.GetCharacterById(Id);
			if(Id == Own || (Id != Target && Idle[Id]) ||
				(pClient ? !pClient->m_Snap.m_aCharacters[Id].m_Active : !p) ||
				(p && (p->m_FreezeTime > 0 || p->Core()->m_DeepFrozen)))
				continue;
			// 2487b0 uses rendered positions, predicted combat state/velocity, and
			// snapshot velocity/weapon when a visible tee has no predicted entity.
			const vec2 OtherPosition = pClient ? pClient->m_aClients[Id].m_RenderPos : p->Core()->m_Pos;
			const float Distance = distance(OtherPosition, SelfPosition);
			if(Distance > Range)
				continue;
			const auto *pCore = p ? p->Core() : nullptr;
			const bool HookingUs = pCore && pCore->HookedPlayer() == Own;
			const float Radius = (p ? p->GetProximityRadius() * 1.25f : 35.f) + pSelf->GetProximityRadius();
			const int Weapon = p ? p->GetActiveWeapon() : pClient->m_Snap.m_aCharacters[Id].m_Cur.m_Weapon;
			CThreat T{OtherPosition, Radius,
				p ? !p->HammerHitDisabled() && (Weapon == WEAPON_HAMMER || HookingUs || Distance <= Radius * 2) : Weapon == WEAPON_HAMMER,
				Weapon == WEAPON_GRENADE && (!p || p->GetWeaponAmmo(WEAPON_GRENADE) != 0),
				HookingUs, Id == Target, pCore && pCore->m_HookState != HOOK_IDLE && !HookingUs, {}};
			vec2 Position = T.m_Position;
			vec2 Velocity = pCore ? pCore->m_Vel : vec2(pClient->m_Snap.m_aCharacters[Id].m_Cur.m_VelX, pClient->m_Snap.m_aCharacters[Id].m_Cur.m_VelY) / 256.f;
			for(int Tick = 0; Tick < m_Horizon; ++Tick)
			{
				Velocity.y += pTune->m_Gravity;
				const vec2 Next = Position + Velocity;
				if(m_pCollision->IntersectLine(Position, Next, nullptr, nullptr))
					break;
				T.m_vFuture.push_back(Next);
				Position = Next;
			}
			m_vThreats.push_back(std::move(T));
		}
	}
	float CSerialThreats::HazardDistance(vec2 Position, vec2 Direction, float Range) const
	{
		if(!m_pCollision || Range <= 0 || length(Direction) < .001f)
			return Range;
		Direction = normalize(Direction);
		for(float D = 14; D <= Range; D += 14)
		{
			const vec2 P = Position + Direction * D;
			if(m_pCollision->CheckPoint(P))
				return Range;
			if(Hazard(m_pCollision, P))
				return D;
		}
		return Range;
	}
	float CSerialThreats::Risk(vec2 Position) const
	{
		float Max = 0, SumOthers = 0;
		for(const auto &T : m_vThreats)
		{
			const float D = distance(T.m_Position, Position);
			if(D > m_Range)
				continue;
			float Risk = 0;
			if(T.m_Hammer)
			{
				vec2 Attacker = T.m_Position;
				int HitTick = D <= T.m_HammerRadius ? 0 : -1;
				if(HitTick < 0)
					for(size_t i = 0; i < T.m_vFuture.size(); ++i)
						if(distance(T.m_vFuture[i], Position) <= T.m_HammerRadius)
						{
							HitTick = i + 1;
							Attacker = T.m_vFuture[i];
							break;
						}
				if(HitTick < 0 && T.m_Target)
				{
					const int Tick = (D - T.m_HammerRadius) / 10;
					if(Tick <= m_Horizon && !m_pCollision->IntersectLine(T.m_Position, Position, nullptr, nullptr))
					{
						HitTick = Tick;
						Attacker = T.m_Position + Unit(Position - T.m_Position) * (D - T.m_HammerRadius);
					}
				}
				if(HitTick >= 0)
				{
					const vec2 Delta = Position - Attacker;
					const vec2 Direction = Unit((length(Delta) <= .001f ? vec2(0, -1) : normalize(Delta)) + vec2(0, -1.1f));
					Risk = DistanceWeight(HazardDistance(Position, Direction, m_Throw), m_Throw) * TimeWeight(HitTick, m_Horizon);
				}
			}
			if(Risk < 1 && D >= .001f && D <= m_HookLength)
			{
				const int Tick = T.m_HookingUs ? 0 : (T.m_OtherHook ? 2 : 0) + (int)(D / 80);
				const float Weight = TimeWeight(Tick, m_Horizon), Range = std::min(D, m_Throw);
				const float R = DistanceWeight(HazardDistance(Position, T.m_Position - Position, Range), Range) * Weight;
				if(R > Risk && !m_pCollision->IntersectLine(Position, T.m_Position, nullptr, nullptr))
					Risk = R;
			}
			if(Risk < 1 && T.m_Grenade)
			{
				if(D < .001f)
					Risk = 1;
				else
				{
					const float Weight = TimeWeight((int)(D / 20), m_Horizon), A = angle(Position - T.m_Position);
					float Best = 0;
					for(int i = 0; i < 3; ++i)
					{
						const float B = A + i * 1.2217306f * .5f - .6108653f;
						Best = std::max(Best, DistanceWeight(HazardDistance(Position, vec2(std::cos(B), std::sin(B)), m_Throw), m_Throw));
					}
					if(Best > 0 && !m_pCollision->IntersectLine(T.m_Position, Position, nullptr, nullptr))
						Risk = std::max(Risk, Best * Weight);
				}
			}
			if(Risk > Max)
			{
				SumOthers += Max;
				Max = Risk;
			}
			else
				SumOthers += Risk;
		}
		return std::clamp(Max + SumOthers * .5f, 0.f, 1.f);
	}
}
