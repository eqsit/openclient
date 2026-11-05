#include "component.h"
#include "projectile_paths.h"
#include <engine/graphics.h>
#include <engine/textrender.h>
#include <engine/shared/client_profile.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/prediction/entities/laser.h>
#include <game/client/projectile_data.h>
#include <game/collision.h>
#include <game/localization.h>
#include <algorithm>
#include <cmath>

namespace OpenKrx::NullClient
{
namespace
{
void Line(IGraphics *pGraphics, vec2 From, vec2 To, ColorRGBA Color, float Width = 1)
{
	const vec2 Delta = To - From;
	if(length(Delta) < .0001f) return;
	const vec2 Normal = vec2(-Delta.y, Delta.x) / length(Delta) * (Width * .5f);
	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	pGraphics->SetColor(Color);
	IGraphics::CFreeformItem Quad(From.x + Normal.x, From.y + Normal.y, To.x + Normal.x, To.y + Normal.y,
		From.x - Normal.x, From.y - Normal.y, To.x - Normal.x, To.y - Normal.y);
	pGraphics->QuadsDrawFreeform(&Quad, 1);
	pGraphics->QuadsEnd();
}
void Ring(IGraphics *pGraphics, vec2 Position, float Radius, float Width, ColorRGBA Color, int Segments)
{
	pGraphics->TextureClear(); pGraphics->QuadsBegin(); pGraphics->SetColor(Color);
	const float Inner = Radius - Width * .5f, Outer = Radius + Width * .5f;
	for(int Index = 0; Index < Segments; ++Index)
	{
		const float A = Index * 6.2831855f / Segments, B = (Index + 1) * 6.2831855f / Segments;
		const vec2 First(std::cos(A), std::sin(A)), Second(std::cos(B), std::sin(B));
		IGraphics::CFreeformItem Quad(Position.x + First.x * Inner, Position.y + First.y * Inner,
			Position.x + Second.x * Inner, Position.y + Second.y * Inner,
			Position.x + First.x * Outer, Position.y + First.y * Outer,
			Position.x + Second.x * Outer, Position.y + Second.y * Outer);
		pGraphics->QuadsDrawFreeform(&Quad, 1);
	}
	pGraphics->QuadsEnd();
}
struct CPoint { vec2 m_Position; bool m_Frozen; };
struct CMarker { vec2 m_Position; int m_Type; };
ColorRGBA PathColor(const std::vector<CPoint> &vPath, size_t Index, float Alpha)
{
	const float Progress = vPath.size() > 1 ? static_cast<float>(Index) / (vPath.size() - 1) : 0;
	const auto Near = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_NullPathlinerColorNear));
	const auto Far = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_NullPathlinerColorFar));
	ColorRGBA Color = vPath[Index].m_Frozen ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_NullPathlinerColorFreeze)) : ColorRGBA(mix(Near.r, Far.r, Progress), mix(Near.g, Far.g, Progress), mix(Near.b, Far.b, Progress), 1);
	Color.a = (g_Config.m_NullPathlinerOpacity / 100.0f) * (g_Config.m_NullPathlinerTaper ? 1 - Progress * .75f : 1) * Alpha;
	return Color;
}
// 140234da0: joined path ribbon, clamped miter amplification [1,2].
vec2 PathNormal(const std::vector<CPoint> &vPath, size_t Index, float HalfWidth)
{
	const auto Unit = [](vec2 V) { return length(V) > .0001f ? normalize(V) : vec2(0, 0); };
	const vec2 Previous = Index ? Unit(vPath[Index].m_Position - vPath[Index - 1].m_Position) : vec2(0, 0);
	const vec2 Next = Index + 1 < vPath.size() ? Unit(vPath[Index + 1].m_Position - vPath[Index].m_Position) : vec2(0, 0);
	vec2 Direction = Unit(Previous + Next);
	if(Direction == vec2(0, 0)) Direction = Next != vec2(0, 0) ? Next : Previous;
	const vec2 Reference = Next != vec2(0, 0) ? Next : Previous;
	const float Denominator = dot(Reference, Direction);
	const float Miter = std::clamp(Denominator > .0001f ? 1.0f / Denominator : 1.0f, 1.0f, 2.0f);
	const float Progress = vPath.size() > 1 ? static_cast<float>(Index) / (vPath.size() - 1) : 0;
	return vec2(-Direction.y, Direction.x) * Miter * HalfWidth * (g_Config.m_NullPathlinerTaper ? 1 - Progress * .55f : 1);
}
void Ribbon(IGraphics *pGraphics, const std::vector<CPoint> &vPath, float Scale, float Alpha)
{
	if(vPath.size() < 2) return;
	pGraphics->TextureClear(); pGraphics->QuadsBegin();
	vec2 PreviousNormal = PathNormal(vPath, 0, g_Config.m_NullPathlinerWidth * .5f * Scale);
	for(size_t Index = 1; Index < vPath.size(); ++Index)
	{
		const vec2 Normal = PathNormal(vPath, Index, g_Config.m_NullPathlinerWidth * .5f * Scale);
		const auto A = PathColor(vPath, Index - 1, Alpha), B = PathColor(vPath, Index, Alpha);
		pGraphics->SetColor4(A, B, A, B);
		const vec2 From = vPath[Index - 1].m_Position, To = vPath[Index].m_Position;
		IGraphics::CFreeformItem Quad(From.x + PreviousNormal.x, From.y + PreviousNormal.y, To.x + Normal.x, To.y + Normal.y,
			From.x - PreviousNormal.x, From.y - PreviousNormal.y, To.x - Normal.x, To.y - Normal.y);
		pGraphics->QuadsDrawFreeform(&Quad, 1);
		PreviousNormal = Normal;
	}
	pGraphics->QuadsEnd();
}
}

void CNullClient::RenderVisuals()
{
	if(!Selected() || Client()->State() != IClient::STATE_ONLINE) return;
	const auto Screen = Graphics()->GetScreen();
	const auto &Camera = GameClient()->m_Camera;
	Graphics()->MapScreen(Graphics()->MapScreenToWorld(Camera.m_Center.x, Camera.m_Center.y, 100, 100, 100, 0, 0, Graphics()->ScreenAspect(), Camera.m_Zoom));
	if(m_Fly.HasMarker())
	{
		auto Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_NullFlymodeColor));
		const float FlyPulse = std::sin(Client()->LocalTime() * 5.f) * .5f + .5f;
		if(g_Config.m_NullFlymodePathDraw && m_Fly.Route().Valid())
		{
			Color.a = .35f;
			const auto &Points = m_Fly.Route().m_vPoints;
			for(size_t i = 1; i < Points.size(); ++i) Line(Graphics(), Points[i - 1], Points[i], Color, 3);
			Color.a = .8f; Ring(Graphics(), m_Fly.Goal(), 6, 1.5f, Color, 32);
		}
		Color.a = .9f; Ring(Graphics(), m_Fly.Marker(), 10, 1.5f, Color, 32);
		Color.a = FlyPulse * .25f + .25f; Ring(Graphics(), m_Fly.Marker(), FlyPulse * 4 + 18, 1, Color, 32);
		Color.a = .9f;
		Line(Graphics(), m_Fly.Marker() - vec2(22, 0), m_Fly.Marker() + vec2(22, 0), Color, 1.5f);
		Line(Graphics(), m_Fly.Marker() - vec2(0, 22), m_Fly.Marker() + vec2(0, 22), Color, 1.5f);
	}
	const int MainSlot = g_Config.m_ClDummy;
	const float Pulse = std::sin(Client()->LocalTime() * 4.0f) * .5f + .5f;
	for(int Slot = 0; Slot < 2; ++Slot)
	{
		const int Id = GameClient()->m_aLocalIds[Slot];
		const auto *pCharacter = GameClient()->KrxPredictedWorld().GetCharacterById(Id);
		if(!pCharacter || Id < 0 || Id >= MAX_CLIENTS) continue;
		const auto &Controller = m_aControllers[Slot];
		const auto &State = Controller.Output();
		const bool Enabled = Slot == MainSlot ? g_Config.m_NullGodmode : g_Config.m_NullGodmodeDummy;
		const vec2 Position = GameClient()->m_aClients[Id].m_RenderPos;
		const int SerialTarget = m_Serial.Target(Slot);
		if(g_Config.m_NullSerialDraw && (Slot == MainSlot ? g_Config.m_NullSerialKiller : g_Config.m_NullSerialKillerDummy) &&
			SerialTarget >= 0 && SerialTarget < MAX_CLIENTS && GameClient()->m_Snap.m_aCharacters[SerialTarget].m_Active)
			Ring(Graphics(), GameClient()->m_aClients[SerialTarget].m_RenderPos, 38, m_Serial.Locked(Slot) ? 3.2f : 1.6f,
				ColorRGBA(1, m_Serial.Locked(Slot) ? .35f : .55f, .35f, m_Serial.Locked(Slot) ? .45f + (std::sin(Client()->LocalTime() * 8) * .5f + .5f) * .2f : .16f + (std::sin(Client()->LocalTime() * 8) * .5f + .5f) * .08f), 28);
		if(Enabled && g_Config.m_NullGodmodeIndicator && (Controller.Active() || State.m_HasPlan || State.m_HasVeto))
		{
			const ColorRGBA Color = State.m_CutTicks > 0 ? ColorRGBA(1, .4f, .8f, .45f + Pulse * .4f) : ColorRGBA(.45f, 1, .65f, .45f + Pulse * .4f);
			Ring(Graphics(), Position, 35.25f + Pulse * 4.0f, 2.5f, Color, 32);
		}
		if(Enabled && g_Config.m_NullGodmodeHammerDodge && g_Config.m_NullGodmodeHammerRing)
		{
			CSimulation Threats;
			if(Threats.Initialize(GameClient()->KrxPredictedWorld(), Id, SettingsFromConfig(), m_Idle.Idle()))
				for(const auto &[Other, Radius] : Threats.HammerThreats())
					Ring(Graphics(), GameClient()->m_aClients[Other].m_RenderPos, Radius, 1, ColorRGBA(1, .45f, .35f, Pulse * .1f + .16f), 36);
		}
		if(g_Config.m_NullGodmodeDebug && Enabled)
		{
			char aText[160];
			str_format(aText, sizeof(aText), "%s  life %d  react %d", Controller.Stage(), Controller.BaselineLife(), Controller.Reaction());
			TextRender()->Text(Position.x + 22, Position.y - 42, 10, aText);
		}
		if(g_Config.m_NullPathliner && (Slot == MainSlot || g_Config.m_NullPathlinerDummy))
		{
			CSimulation World;
			if(World.Initialize(GameClient()->KrxPredictedWorld(), Id, SettingsFromConfig(), m_Idle.Idle()))
			{
				auto Input = Slot == MainSlot ? GameClient()->m_Controls.m_aInputData[Slot] : GameClient()->m_DummyInput;
				std::vector<CPoint> vPath{{World.Position(), World.Frozen()}};
				std::vector<CMarker> vMarkers;
				bool WasFrozen = World.Frozen(), WasGrounded = World.Grounded(), WasHooked = pCharacter->Core()->m_HookState == HOOK_GRABBED;
				for(int Tick = 1; Tick <= g_Config.m_NullPathlinerTicks; ++Tick)
				{
					if(!World.Tick(Input)) break;
					const bool Frozen = World.Frozen(), Grounded = World.Grounded();
					vPath.push_back({World.Position(), Frozen});
					if(Frozen && !WasFrozen) vMarkers.push_back({World.Position(), 0});
					if(Grounded && !WasGrounded) vMarkers.push_back({World.Position(), 2});
					if(const auto *pLocal = World.Character())
					{
						const bool Hooked = pLocal->Core()->m_HookState == HOOK_GRABBED;
						if(Hooked && !WasHooked) vMarkers.push_back({pLocal->Core()->m_HookPos, 3});
						WasHooked = Hooked;
					}
					if(World.Dead()) { vMarkers.push_back({World.Position(), 1}); break; }
					if(g_Config.m_NullPathlinerStopOnFreeze && Frozen && length(World.Velocity()) < .15f) break;
					WasFrozen = Frozen; WasGrounded = Grounded;
				}
				if(vPath.size() > 1) vMarkers.push_back({vPath.back().m_Position, 4});
				if(g_Config.m_NullPathlinerGlow)
				{
					Ribbon(Graphics(), vPath, 4, .035f);
					Ribbon(Graphics(), vPath, 2, .09f);
				}
				Ribbon(Graphics(), vPath, 1, 1);
				if(g_Config.m_NullPathlinerFlow)
				{
					float Along = 0;
					const float Phase = Client()->LocalTime() * g_Config.m_NullPathlinerFlowSpeed;
					for(size_t Index = 1; Index < vPath.size(); ++Index)
					{
						const float Length = distance(vPath[Index - 1].m_Position, vPath[Index].m_Position);
						const float Wave = std::pow(std::max(0.0f, std::cos(((Along - Phase) / 260.0f) * 6.2831855f)), 6.0f);
						if(Wave >= .01f)
						{
							auto Color = PathColor(vPath, Index, Wave * .8f);
							Color.r = mix(Color.r, 1.0f, .5f); Color.g = mix(Color.g, 1.0f, .5f); Color.b = mix(Color.b, 1.0f, .5f);
							Line(Graphics(), vPath[Index - 1].m_Position, vPath[Index].m_Position, Color, g_Config.m_NullPathlinerWidth);
						}
						Along += Length;
					}
				}
				const bool aMarkerEnabled[] = {g_Config.m_NullPathlinerFreezeMarker != 0, g_Config.m_NullPathlinerDeathMarker != 0, g_Config.m_NullPathlinerLandingMarker != 0, g_Config.m_NullPathlinerHookMarker != 0, g_Config.m_NullPathlinerEndMarker != 0};
				for(const auto &Marker : vMarkers)
					if(aMarkerEnabled[Marker.m_Type])
						Ring(Graphics(), Marker.m_Position, Marker.m_Type == 1 ? 8 : 5, 1.5f,
							Marker.m_Type == 1 ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_NullPathlinerColorDeath)) : PathColor(vPath, vPath.size() - 1, 1), 16);
			}
		}
	}
	RenderWeaponLines();
	Graphics()->MapScreen(Screen);
	if(g_Config.m_NullHud)
	{
		const float Height = 300, Width = Height * Graphics()->ScreenAspect();
		const float Size = 6 * g_Config.m_NullHudSize / 100.0f;
		struct CFeature { const char *m_pName; bool m_On; };
		const CFeature aFeatures[] = {{"Avoid Freeze", g_Config.m_NullGodmode != 0}, {"Avoid Freeze (dummy)", g_Config.m_NullGodmodeDummy != 0}, {"Serial killer", g_Config.m_NullSerialKiller != 0}, {"Serial killer (dummy)", g_Config.m_NullSerialKillerDummy != 0}, {"Aimbot", g_Config.m_NullAimbot != 0}, {"Fly mode", g_Config.m_NullFlymode != 0}, {"Fake aim", g_Config.m_NullFakeAim != 0}};
		int Rows = 0;
		for(const auto &Feature : aFeatures) Rows += Feature.m_On || g_Config.m_NullHudShowOff;
		if(Rows == 0)
			return;
		Graphics()->MapScreen(CScreenRect(0, 0, Width, Height));
		const float H = Rows * (Size + 3) + 8, W = 108 * g_Config.m_NullHudSize / 100.0f;
		const int Place = g_Config.m_NullHudPos;
		const float X = (Place & 1) ? Width - W - 6 : 6;
		float Y = Place >= 4 ? (Height - H) * .5f : Place >= 2 ? Height - H - 6 : 6;
		Graphics()->DrawRect(X, Y, W, H, ColorRGBA(0, 0, 0, .55f), IGraphics::CORNER_ALL, 3);
		Y += 4;
		for(const auto &Feature : aFeatures)
			if(Feature.m_On || g_Config.m_NullHudShowOff)
			{
				TextRender()->TextColor(Feature.m_On ? ColorRGBA(.65f, 1, .72f, 1) : ColorRGBA(1, 1, 1, .35f));
				TextRender()->Text(X + 5, Y, Size, Localize(Feature.m_pName));
				Y += Size + 3;
			}
		TextRender()->TextColor(1, 1, 1, 1);
		Graphics()->MapScreen(Screen);
	}
}

void CNullClient::RenderWeaponLines()
{
	auto &Source = GameClient()->KrxPredictedWorld();
	const int Own = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(g_Config.m_NullAimbot && !GameClient()->m_Snap.m_SpecInfo.m_Active)
		if(auto *pSelf = Source.GetCharacterById(Own))
		{
			const auto &Tune = *pSelf->GetTuning(pSelf->GetPureTuneZone());
			const float Radius = pSelf->GetProximityRadius();
			const float aRanges[] = {static_cast<float>(Tune.m_HookLength) + 28, Radius * 1.25f + 44,
				static_cast<float>(Tune.m_GunSpeed) * static_cast<float>(Tune.m_GunLifetime),
				Source.m_WorldConfig.m_IsDDRace ? static_cast<float>(Tune.m_LaserReach) : static_cast<float>(Tune.m_ShotgunSpeed) * static_cast<float>(Tune.m_ShotgunLifetime),
				static_cast<float>(Tune.m_GrenadeSpeed) * static_cast<float>(Tune.m_GrenadeLifetime), static_cast<float>(Tune.m_LaserReach)};
			const int aModes[] = {g_Config.m_NullAimbotDrawHook, g_Config.m_NullAimbotDrawHammer, g_Config.m_NullAimbotDrawGun, g_Config.m_NullAimbotDrawShotgun, g_Config.m_NullAimbotDrawGrenade, g_Config.m_NullAimbotDrawLaser};
			const int aAlpha[] = {g_Config.m_NullAimbotAlphaHook, g_Config.m_NullAimbotAlphaHammer, g_Config.m_NullAimbotAlphaGun, g_Config.m_NullAimbotAlphaShotgun, g_Config.m_NullAimbotAlphaGrenade, g_Config.m_NullAimbotAlphaLaser};
			const int aFov[] = {g_Config.m_NullAimbotFovHook, g_Config.m_NullAimbotFovHammer, g_Config.m_NullAimbotFovGun, g_Config.m_NullAimbotFovShotgun, g_Config.m_NullAimbotFovGrenade, g_Config.m_NullAimbotFovLaser};
			const ColorRGBA aColors[] = {{.4f, 1, .6f, 1}, {1, .65f, .25f, 1}, {.6f, .8f, 1, 1}, {1, .9f, .3f, 1}, {1, .45f, .35f, 1}, {.7f, .5f, 1, 1}};
			const float Facing = angle(GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy]);
			const vec2 Position = GameClient()->m_aClients[Own].m_RenderPos;
			for(int Kind = 0; Kind < 6; ++Kind)
			{
				const bool Held = Kind == 0 ? pSelf->Core()->m_HookState == HOOK_FLYING || pSelf->Core()->m_HookState == HOOK_GRABBED : Kind - 1 == pSelf->GetActiveWeapon();
				if(aModes[Kind] == 0 || (aModes[Kind] == 2 && !Held)) continue;
				const float Half = std::min(180.0f, aFov[Kind] * .5f) * 3.1415927f / 180.0f;
				const auto Color = aColors[Kind].WithAlpha(aAlpha[Kind] / 100.0f);
				if(Half < 3.138451f)
					for(float Side : {-1.0f, 1.0f})
					{
						const float A = Facing + Side * Half;
						const vec2 Direction(std::cos(A), std::sin(A));
						Line(Graphics(), Position + Direction * Radius, Position + Direction * aRanges[Kind], Color, 1.4f);
					}
				const int Segments = std::max(2, static_cast<int>(aFov[Kind] * .25f));
				for(int Segment = 0; Segment < Segments; ++Segment)
				{
					const float A = Facing - Half + (Half + Half) * Segment / Segments;
					const float B = Facing - Half + (Half + Half) * (Segment + 1) / Segments;
					Line(Graphics(), Position + vec2(std::cos(A), std::sin(A)) * aRanges[Kind], Position + vec2(std::cos(B), std::sin(B)) * aRanges[Kind], Color, 1.4f);
				}
			}
			const int Target = m_Aimbot.Target(g_Config.m_ClDummy);
			if(Target >= 0 && Target < MAX_CLIENTS && GameClient()->m_Snap.m_aCharacters[Target].m_Active)
			{
				const int Kind = std::clamp(m_Aimbot.Weapon(g_Config.m_ClDummy) + 1, 0, 5);
				Ring(Graphics(), GameClient()->m_aClients[Target].m_RenderPos, 34, 2,
					aColors[Kind].WithAlpha((std::sin(Client()->LocalTime() * 6) * .5f + .5f) * .15f + .32f), 24);
			}
		}
	const auto ProjectileEnabled = [](int Weapon, bool Aim) {
		if(Weapon == WEAPON_GUN) return Aim ? g_Config.m_NullAimLineGun : g_Config.m_NullBulletLineGun;
		if(Weapon == WEAPON_SHOTGUN) return Aim ? g_Config.m_NullAimLineShotgun : g_Config.m_NullBulletLineShotgun;
		if(Weapon == WEAPON_GRENADE) return Aim ? g_Config.m_NullAimLineGrenade : g_Config.m_NullBulletLineGrenade;
		return 0;
	};
	if(g_Config.m_NullAimLines)
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		{
			auto *pCharacter = Source.GetCharacterById(Id);
			if(!pCharacter || (g_Config.m_NullAimLineSelfOnly && Id != Own)) continue;
			const int Weapon = pCharacter->GetActiveWeapon();
			const bool Laser = Weapon == WEAPON_LASER || (Weapon == WEAPON_SHOTGUN && Source.m_WorldConfig.m_IsDDRace);
			if(Laser ? !(Weapon == WEAPON_LASER ? g_Config.m_NullAimLineLaser : g_Config.m_NullAimLineShotgun) : !ProjectileEnabled(Weapon, true)) continue;
			const auto *pTune = pCharacter->GetTuning(pCharacter->GetPureTuneZone());
			const auto &Input = Id == Own ? GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy] : pCharacter->DiagnosticCurrentInput();
			const vec2 Direction = normalize(vec2(Input.m_TargetX, Input.m_TargetY));
			if(Laser)
			{
				CGameWorld Probe; Probe.CopyWorldIsolated(&Source);
				auto *pLaser = new CLaser(&Probe, pCharacter->Core()->m_Pos, Direction, pTune->m_LaserReach, Id, Weapon);
				int LastEval = -1;
				for(int Tick = 0; Tick < g_Config.m_NullAimLineTicks; ++Tick)
				{
					bool Present = false;
					for(auto *p = Probe.FindFirst(CGameWorld::ENTTYPE_LASER); p; p = p->TypeNext()) if(p == pLaser) Present = true;
					if(!Present) break;
					if(pLaser->GetEvalTick() != LastEval)
					{
						Line(Graphics(), pLaser->GetFrom(), pLaser->GetPos(), ColorRGBA(.8f, .8f, 1, g_Config.m_NullAimLineAlpha / 100.0f));
						LastEval = pLaser->GetEvalTick();
					}
					Probe.m_GameTick++; Probe.Tick();
				}
			}
			else
			{
				for(const auto &Path : AimProjectilePath(Source, Id, Weapon, Direction, g_Config.m_NullAimLineTicks))
					for(size_t i = 1; i < Path.m_vPoints.size(); ++i)
						Line(Graphics(), Path.m_vPoints[i - 1], Path.m_vPoints[i], ColorRGBA(.9f, .9f, .9f, g_Config.m_NullAimLineAlpha / 100.f));
			}
		}
	if(g_Config.m_NullGodmodeShotPath)
		for(const auto &Path : ProjectilePaths(Source, g_Config.m_NullGodmodeShotPathTicks))
		{
			if(!ProjectileEnabled(Path.m_Weapon, false) || (g_Config.m_NullBulletLineSelfOnly && Path.m_Owner != Own)) continue;
			for(size_t i = 1; i < Path.m_vPoints.size(); ++i)
				Line(Graphics(), Path.m_vPoints[i - 1], Path.m_vPoints[i], ColorRGBA(1, .55f, .4f, g_Config.m_NullBulletLineAlpha / 100.0f));
			if(Path.m_Impact && Path.m_Weapon == WEAPON_GRENADE && !Path.m_vPoints.empty())
				Ring(Graphics(), Path.m_vPoints.back(), 70, 1, ColorRGBA(1, .4f, .3f, .3f), 32);
		}
}
}
