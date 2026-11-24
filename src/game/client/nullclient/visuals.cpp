#include "component.h"
#include "projectile_paths.h"
#include <base/time.h>
#include <engine/demo.h>
#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/textrender.h>
#include <engine/shared/client_profile.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/projectile.h>
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
	const float Inner = std::max(0.f, Radius - Width * .5f), Outer = Radius + Width * .5f;
	for(int Index = 0; Index < Segments; ++Index)
	{
		const float A = Index * 6.2831855f / Segments, B = (Index + 1) * 6.2831855f / Segments;
		const vec2 First(std::cos(A), std::sin(A)), Second(std::cos(B), std::sin(B));
		IGraphics::CFreeformItem Quad(Position + First * Outer, Position + Second * Outer,
			Position + First * Inner, Position + Second * Inner);
		pGraphics->QuadsDrawFreeform(&Quad, 1);
	}
	pGraphics->QuadsEnd();
}
struct CPoint { vec2 m_Position; bool m_Frozen; };
enum class EMarker { FREEZE, DEATH, LANDING, HOOK, END };
struct CMarker { vec2 m_Position; vec2 m_Direction; EMarker m_Type; };
ColorRGBA PathColor(const std::vector<CPoint> &vPath, size_t Index, float Alpha)
{
	const float Progress = vPath.size() > 1 ? static_cast<float>(Index) / (vPath.size() - 1) : 0;
	const auto Near = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_NullPathlinerColorNear));
	const auto Far = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_NullPathlinerColorFar));
	ColorRGBA Color = vPath[Index].m_Frozen ? color_cast<ColorRGBA>(ColorHSLA(g_Config.m_NullPathlinerColorFreeze)) : ColorRGBA(mix(Near.r, Far.r, Progress), mix(Near.g, Far.g, Progress), mix(Near.b, Far.b, Progress), 1);
	Color.a = (g_Config.m_NullPathlinerOpacity / 100.0f) * (g_Config.m_NullPathlinerTaper ? 1 - Progress * .75f : 1) * Alpha;
	return Color;
}
// Clamp the miter at sharp joins to twice the half-width.
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

float FlowWave(float Along, float Phase)
{
	return std::pow(std::max(0.0f, std::sin((Along - Phase) / 260.0f * 2.0f * pi)), 6.0f);
}

void DrawFlow(IGraphics *pGraphics, const std::vector<CPoint> &vPath, float Phase)
{
	float Along = 0;
	float Wave = FlowWave(Along, Phase);
	pGraphics->BlendAdditive();
	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	for(size_t i = 1; i < vPath.size(); ++i)
	{
		const vec2 A = vPath[i - 1].m_Position, B = vPath[i].m_Position;
		const vec2 Delta = B - A;
		const float Length = length(Delta);
		if(Length < .0001f)
			continue;
		Along += Length;
		const float Next = FlowWave(Along, Phase);
		if(Wave >= .01f || Next >= .01f)
		{
			const auto Brighten = [](ColorRGBA Color) {
				return ColorRGBA(mix(Color.r, 1.f, .5f), mix(Color.g, 1.f, .5f), mix(Color.b, 1.f, .5f), Color.a);
			};
			const auto From = Brighten(PathColor(vPath, i - 1, Wave * .8f));
			const auto To = Brighten(PathColor(vPath, i, Next * .8f));
			pGraphics->SetColor4(From, To, From, To);
			const vec2 N = vec2(-Delta.y, Delta.x) / Length * (g_Config.m_NullPathlinerWidth * .5f);
			IGraphics::CFreeformItem Quad(A.x + N.x, A.y + N.y, B.x + N.x, B.y + N.y, A.x - N.x, A.y - N.y, B.x - N.x, B.y - N.y);
			pGraphics->QuadsDrawFreeform(&Quad, 1);
		}
		Wave = Next;
	}
	pGraphics->QuadsEnd();
	pGraphics->BlendNormal();
}

void DrawPathMarker(IGraphics *pGraphics, const CMarker &Marker, float Pulse)
{
	const vec2 P = Marker.m_Position;
	const float Size = std::max(5, g_Config.m_NullPathlinerWidth);
	const float Alpha = g_Config.m_NullPathlinerOpacity / 100.f;
	const auto Far = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_NullPathlinerColorFar));
	const auto Stroke = [&](vec2 Center, vec2 Direction, float Span, float Width, ColorRGBA Color) {
		const vec2 Half = Direction * (.5f * Span * Size);
		Line(pGraphics, Center - Half, Center + Half, Color, Width * Size);
	};
	switch(Marker.m_Type)
	{
	case EMarker::FREEZE:
		if(g_Config.m_NullPathlinerFreezeMarker)
		{
			const auto Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_NullPathlinerColorFreeze));
			Ring(pGraphics, P, (1.6f + .25f * Pulse) * Size, .35f * Size, Color.WithAlpha(Alpha), 24);
			for(int i = 0; i < 3; ++i)
				Stroke(P, direction(i * pi / 3.f), 1.6f, .25f, Color.WithAlpha(.9f * Alpha));
		}
		break;
	case EMarker::DEATH:
		if(g_Config.m_NullPathlinerDeathMarker)
		{
			const auto Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_NullPathlinerColorDeath));
			const float Radius = (1.8f + .4f * Pulse) * Size;
			Ring(pGraphics, P, Radius, .4f * Size, Color.WithAlpha(Alpha), 24);
			Ring(pGraphics, P, Radius * 1.7f, .18f * Size, Color.WithAlpha((1.f - Pulse) * Alpha * .4f), 24);
			for(int i = 0; i < 2; ++i)
				Stroke(P, direction(i * pi / 2.f + pi / 4.f), 2.2f, .35f, Color.WithAlpha(Alpha));
		}
		break;
	case EMarker::LANDING:
		if(g_Config.m_NullPathlinerLandingMarker)
			Stroke(P + vec2(0, 1.2f * Size), vec2(1, 0), 2, .3f, Far.WithAlpha(Alpha * .7f));
		break;
	case EMarker::HOOK:
		if(g_Config.m_NullPathlinerHookMarker)
			Ring(pGraphics, P, Size * 1.1f, Size * .3f, Far.WithAlpha(Alpha * .8f), 24);
		break;
	case EMarker::END:
		if(g_Config.m_NullPathlinerEndMarker)
		{
			const float EndAlpha = Alpha * (g_Config.m_NullPathlinerTaper ? .35f : 1.f);
			const vec2 Dir = Marker.m_Direction;
			if(Dir != vec2(0, 0))
			{
				const vec2 Base = P - Dir * (2 * Size), Side = vec2(-Dir.y, Dir.x) * Size;
				IGraphics::CFreeformItem Arrow(P.x, P.y, P.x, P.y, Base.x + Side.x, Base.y + Side.y, Base.x - Side.x, Base.y - Side.y);
				pGraphics->TextureClear();
				pGraphics->QuadsBegin();
				pGraphics->SetColor(Far.WithAlpha(EndAlpha));
				pGraphics->QuadsDrawFreeform(&Arrow, 1);
				pGraphics->QuadsEnd();
			}
			Ring(pGraphics, P, (1.f + .5f * Pulse) * Size, .2f * Size, Far.WithAlpha(EndAlpha * (1.f - Pulse)), 24);
		}
		break;
	}
}

float RescueElapsed(std::int64_t Now, std::int64_t Start)
{
	return static_cast<float>(Now - Start) / time_freq();
}

void DrawRescuePaths(IGraphics *pGraphics, const std::vector<CRescuePath> &vPaths, size_t Budget, float Age, float Lifetime, bool Scene, bool Selected)
{
	if(Age < 0 || Age > Lifetime || vPaths.empty())
		return;
	const float Fade = std::clamp((Lifetime - Age) / std::min(Lifetime, 0.5f), 0.0f, 1.0f);
	const size_t Stride = std::max<size_t>(1, (vPaths.size() + Budget - 1) / Budget);
	const int Animation = g_Config.m_NullGodmodeVisualAnimation;
	const float Speed = g_Config.m_NullGodmodeVisualSpeed;
	pGraphics->TextureClear();
	pGraphics->LinesBegin();
	for(size_t Index = 0; Index < vPaths.size(); ++Index)
	{
		const auto &Path = vPaths[Index];
		if(Path.m_Selected != Selected || Path.m_vPoints.size() < 2)
			continue;
		if(!Selected && (Index % Stride != 0 || (!Scene && g_Config.m_NullGodmodeVisualsSelected)))
			continue;
		float Progress = 1.0f;
		float Opacity = Fade;
		if(Scene)
		{
			const float Phase = Age * Speed / 100.0f;
			Progress = Selected ? (Phase - 1.35f) / 0.55f : Phase / 0.4f;
			if(!Selected)
				Opacity *= std::clamp((1.65f - Phase) / 0.3f, 0.0f, 1.0f);
		}
		else if(Animation != 0)
		{
			const float Delay = Selected ? 0.0f : static_cast<float>(Index % 11) * 0.018f;
			Progress = std::max(0.0f, Age * Speed / 80.0f - Delay);
			if(Animation == 2)
				Progress = std::fmod(Progress, 1.0f);
		}
		Progress = std::clamp(Progress, 0.0f, 1.0f);
		if(Progress == 0.0f || Opacity == 0.0f)
			continue;
		ColorRGBA Color = Selected ?
			ColorRGBA(1.0f, 0.75f, 0.35f, 0.65f) :
			(Path.m_Safe ? ColorRGBA(0.5f, 0.75f, 1.0f, 0.2f) : ColorRGBA(1.0f, 0.5f, 0.35f, 0.16f));
		if(Selected && Path.m_Safe)
			Color = Scene ? ColorRGBA(.25f, 1.f, .35f, .9f) : ColorRGBA(.4f, 1.f, .85f, .8f);
		Color.a *= Opacity;
		pGraphics->SetColor(Color);
		const float Count = static_cast<float>(Path.m_vPoints.size() - 1) * Progress;
		vec2 Head = Path.m_vPoints.front();
		for(size_t Point = 1; static_cast<float>(Point - 1) < Count && Point < Path.m_vPoints.size(); ++Point)
		{
			const vec2 From = Path.m_vPoints[Point - 1];
			const vec2 To = Path.m_vPoints[Point];
			Head = mix(From, To, std::min(1.0f, Count - static_cast<float>(Point - 1)));
			if(distance(From, To) > 160.0f)
				continue;
			const IGraphics::CLineItem Item(From.x, From.y, Head.x, Head.y);
			pGraphics->LinesDraw(&Item, 1);
		}
		if(Progress == 1.0f && g_Config.m_NullGodmodeVisualEnds && Path.m_Endpoint != ERescueEndpoint::NONE)
		{
			const bool Freeze = Path.m_Endpoint == ERescueEndpoint::FREEZE;
			pGraphics->SetColor(ColorRGBA(1.0f, Freeze ? 0.3f : 0.75f, 0.3f, Opacity * (Freeze ? 0.7f : 0.6f)));
			const IGraphics::CLineItem aCross[] = {
				{Head.x - 3.0f, Head.y - 3.0f, Head.x + 3.0f, Head.y + 3.0f},
				{Head.x - 3.0f, Head.y + 3.0f, Head.x + 3.0f, Head.y - 3.0f}};
			pGraphics->LinesDraw(aCross, std::size(aCross));
		}
		else if(Animation != 0 && Progress < 1.0f)
		{
			const IGraphics::CLineItem Marker(Head.x - 1.5f, Head.y, Head.x + 1.5f, Head.y);
			pGraphics->LinesDraw(&Marker, 1);
		}
	}
	pGraphics->LinesEnd();
}
}

bool CNullClient::CaptureRescueScene()
{
	return Selected() && Client()->State() == IClient::STATE_ONLINE && m_SavedRescue.has_value();
}

bool CNullClient::StartRescueScene(bool LogErrors)
{
	if(!Selected() || Client()->State() != IClient::STATE_DEMOPLAYBACK ||
		!m_SavedRescue || GameClient()->Map()->Crc() != m_SavedRescue->m_MapCrc)
	{
		if(LogErrors)
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/rescue", "Open the demo of the captured run on the same map.");
		return false;
	}
	const auto *pInfo = DemoPlayer()->BaseInfo();
	const int Tick = m_SavedRescue->m_Tick;
	if(pInfo->m_LiveDemo || Tick < pInfo->m_FirstTick || Tick > pInfo->m_LastTick)
	{
		if(LogErrors)
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/rescue", "The captured tick is unavailable in this demo.");
		return false;
	}
	const bool WasPaused = pInfo->m_Paused;
	DemoPlayer()->Pause();
	if(!DemoPlayer()->SetPos(Tick))
	{
		if(!WasPaused && DemoPlayer()->IsPlaying())
			DemoPlayer()->Unpause();
		return false;
	}
	DemoPlayer()->Pause();
	m_RescueSceneStart = time_get_impl();
	m_RescueSceneShown = true;
	return true;
}

void CNullClient::StopRescueScene()
{
	if(m_RescueSceneStart == 0)
		return;
	m_RescueSceneStart = 0;
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		DemoPlayer()->Unpause();
}

void CNullClient::RenderRescueScene()
{
	if(!m_RescueSceneShown && g_Config.m_NullGodmodeVisuals && m_SavedRescue &&
		DemoPlayer()->BaseInfo()->m_CurrentTick >= m_SavedRescue->m_Tick)
		StartRescueScene(false);
	if(m_RescueSceneStart == 0 || !m_SavedRescue)
		return;
	const float Lifetime = 240.0f / g_Config.m_NullGodmodeVisualSpeed;
	const float Age = RescueElapsed(time_get_impl(), m_RescueSceneStart);
	if(Age >= Lifetime)
	{
		StopRescueScene();
		return;
	}
	const auto Screen = Graphics()->GetScreen();
	const auto &Camera = GameClient()->m_Camera;
	Graphics()->MapScreen(Graphics()->MapScreenToWorld(Camera.m_Center.x, Camera.m_Center.y, 100, 100, 100, 0, 0, Graphics()->ScreenAspect(), Camera.m_Zoom));
	for(bool Selected : {false, true})
		DrawRescuePaths(Graphics(), m_SavedRescue->m_vPaths, g_Config.m_NullGodmodeVisualCount, Age, Lifetime, true, Selected);
	Graphics()->MapScreen(Screen);
}

void CNullClient::RenderRescuePaths()
{
	if(!g_Config.m_NullGodmodeVisuals || m_vRescueSnapshots.empty())
		return;
	const size_t History = std::min(m_vRescueSnapshots.size(), static_cast<size_t>(g_Config.m_NullGodmodeVisualHistory));
	const size_t Budget = std::max<size_t>(1, g_Config.m_NullGodmodeVisualCount / History);
	const auto Now = time_get_impl();
	const float Lifetime = g_Config.m_NullGodmodeVisualDuration / 1000.0f;
	for(bool Selected : {false, true})
		for(size_t Index = m_vRescueSnapshots.size() - History; Index < m_vRescueSnapshots.size(); ++Index)
		{
			const auto &Snapshot = m_vRescueSnapshots[Index];
			DrawRescuePaths(Graphics(), Snapshot.m_vPaths, Budget, RescueElapsed(Now, Snapshot.m_Time), Lifetime, false, Selected);
		}
}

void CNullClient::RenderVisuals()
{
	if(!Selected() || Client()->State() != IClient::STATE_ONLINE) return;
	const auto Screen = Graphics()->GetScreen();
	const auto &Camera = GameClient()->m_Camera;
	Graphics()->MapScreen(Graphics()->MapScreenToWorld(Camera.m_Center.x, Camera.m_Center.y, 100, 100, 100, 0, 0, Graphics()->ScreenAspect(), Camera.m_Zoom));
	RenderRescuePaths();
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
			for(const auto &[Other, Radius] : Controller.HammerThreats())
				if(Other >= 0 && Other < MAX_CLIENTS && GameClient()->m_Snap.m_aCharacters[Other].m_Active)
					Ring(Graphics(), GameClient()->m_aClients[Other].m_RenderPos, Radius, 1, ColorRGBA(1, .45f, .35f, Pulse * .1f + .16f), 36);
		}
		if(Enabled && g_Config.m_NullGodmodeAvoidRocket && g_Config.m_NullGodmodeAvoidRocketRing)
		{
			for(const auto &[Other, Radius] : Controller.RocketContactThreats())
				if(Other >= 0 && Other < MAX_CLIENTS && GameClient()->m_Snap.m_aCharacters[Other].m_Active)
					Ring(Graphics(), GameClient()->m_aClients[Other].m_RenderPos, Radius, 1, ColorRGBA(1, .65f, .25f, Pulse * .1f + .16f), 36);
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
			if(World.InitializePathliner(GameClient()->KrxPredictedWorld(), Id))
			{
				const auto Input = SimulationInput(Slot);
				const auto FrozenForPath = [&World]() {
					const auto *pLocal = World.Character();
					return pLocal && (pLocal->m_FreezeTime > 0 || pLocal->Core()->m_DeepFrozen);
				};
				bool WasFrozen = FrozenForPath(), WasGrounded = World.Grounded(), WasHooked = World.Character()->Core()->m_HookState == HOOK_GRABBED;
				std::vector<CPoint> vPath{{World.Position(), WasFrozen}};
				std::vector<CMarker> vMarkers;
				vec2 Direction(0, 0);
				float TotalLength = 0;
				bool Died = false;
				for(int Tick = 1; Tick <= g_Config.m_NullPathlinerTicks; ++Tick)
				{
					if(!World.Tick(Input))
						break;
					const vec2 PathPos = World.Position();
					const vec2 Delta = PathPos - vPath.back().m_Position;
					const float Length = length(Delta);
					TotalLength += Length;
					Direction = Length > .0001f ? Delta / Length : vec2(0, 0);
					const bool Frozen = FrozenForPath(), Grounded = World.Grounded();
					vPath.push_back({PathPos, Frozen});
					if(Frozen && !WasFrozen)
						vMarkers.push_back({PathPos, Direction, EMarker::FREEZE});
					if(Grounded && !WasGrounded)
						vMarkers.push_back({PathPos, Direction, EMarker::LANDING});
					if(const auto *pLocal = World.Character())
					{
						const bool Hooked = pLocal->Core()->m_HookState == HOOK_GRABBED;
						if(Hooked && !WasHooked)
							vMarkers.push_back({pLocal->Core()->m_HookPos, Direction, EMarker::HOOK});
						WasHooked = Hooked;
					}
					if(World.Dead())
					{
						vMarkers.push_back({PathPos, Direction, EMarker::DEATH});
						Died = true;
						break;
					}
					if(g_Config.m_NullPathlinerStopOnFreeze && Frozen && length(World.Velocity()) < .15f)
						break;
					WasFrozen = Frozen;
					WasGrounded = Grounded;
				}
				if(TotalLength < 2.f)
					continue;
				if(!Died)
					vMarkers.push_back({vPath.back().m_Position, Direction, EMarker::END});
				if(g_Config.m_NullPathlinerGlow)
					Ribbon(Graphics(), vPath, 2.2f, .12f);
				Ribbon(Graphics(), vPath, 1, 1);
				if(g_Config.m_NullPathlinerFlow)
					DrawFlow(Graphics(), vPath, Client()->LocalTime() * g_Config.m_NullPathlinerFlowSpeed);
				for(const auto &Marker : vMarkers)
					DrawPathMarker(Graphics(), Marker, Pulse);
			}
		}
	}
	RenderWeaponLines();
	Graphics()->MapScreen(Screen);
	if(g_Config.m_NullHud)
	{
		const float Height = 300, Width = Height * Graphics()->ScreenAspect();
		// 23eee0: size follows the longest localized label, with a separate
		// status dot. A fixed 108-unit panel clips long translations.
		const float Scale = std::clamp(g_Config.m_NullHudSize, 30, 250) / 100.f;
		const float Size = 6.5f * Scale, Padding = 4.5f * Scale;
		const float DotSize = 3.4f * Scale, Gap = 3.5f * Scale;
		struct CFeature { const char *m_pName; bool m_On; };
		const CFeature aFeatures[] = {{"Avoid Freeze", g_Config.m_NullGodmode != 0}, {"Avoid Freeze (dummy)", g_Config.m_NullGodmodeDummy != 0}, {"Serial killer", g_Config.m_NullSerialKiller != 0}, {"Serial killer (dummy)", g_Config.m_NullSerialKillerDummy != 0}, {"Aimbot", g_Config.m_NullAimbot != 0}, {"Fly mode", g_Config.m_NullFlymode != 0}, {"Fake aim", g_Config.m_NullFakeAim != 0}};
		Graphics()->MapScreen(CScreenRect(0, 0, Width, Height));
		int Rows = 0;
		float TextWidth = 0;
		for(const auto &Feature : aFeatures)
			if(Feature.m_On || g_Config.m_NullHudShowOff)
			{
				++Rows;
				TextWidth = std::max(TextWidth, TextRender()->TextWidth(Size, Localize(Feature.m_pName)));
			}
		if(Rows == 0)
		{
			Graphics()->MapScreen(Screen);
			return;
		}
		const float H = Rows * (Size + Gap) + Padding * 2 - 1.5f * Scale;
		const float W = Padding * 2 + DotSize + Gap + TextWidth, Margin = 6.f * Scale;
		const int Place = std::clamp(g_Config.m_NullHudPos, 0, 5);
		const float X = (Place & 1) ? Width - W - Margin : Margin;
		float Y = Place >= 4 ? (Height - H) * .5f : Place >= 2 ? Height - H - Margin : Margin;
		Graphics()->DrawRect(X, Y, W, H, ColorRGBA(0, 0, 0, .38f), IGraphics::CORNER_ALL, 3.f * Scale);
		Y += Padding;
		for(const auto &Feature : aFeatures)
			if(Feature.m_On || g_Config.m_NullHudShowOff)
			{
				Graphics()->DrawRect(X + Padding, Y + (Size - DotSize) * .5f, DotSize, DotSize,
					Feature.m_On ? ColorRGBA(.3f, .95f, .55f, .95f) : ColorRGBA(.65f, .65f, .65f, .45f), IGraphics::CORNER_ALL, DotSize * .5f);
				TextRender()->TextColor(1, 1, 1, Feature.m_On ? .92f : .38f);
				TextRender()->Text(X + Padding + DotSize + Gap, Y, Size, Localize(Feature.m_pName));
				Y += Size + Gap;
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
			const int Target = m_Aimbot.Target();
			if(Target >= 0 && Target < MAX_CLIENTS && GameClient()->m_Snap.m_aCharacters[Target].m_Active)
			{
				const int Kind = std::clamp(m_Aimbot.Weapon() + 1, 0, 5);
				Ring(Graphics(), GameClient()->m_aClients[Target].m_RenderPos, 34, 2,
					aColors[Kind].WithAlpha((std::sin(Client()->LocalTime() * 6) * .5f + .5f) * .15f + .32f), 24);
			}
		}
	if(g_Config.m_NullGodmodeDebug && g_Config.m_NullAimbot && !GameClient()->m_Snap.m_SpecInfo.m_Active &&
		Own >= 0 && Own < MAX_CLIENTS && GameClient()->m_Snap.m_aCharacters[Own].m_Active)
	{
		char aMessage[96];
		if(m_Aimbot.DebugMessage(aMessage, sizeof(aMessage)))
		{
			const auto PreviousColor = TextRender()->GetTextColor();
			const vec2 Position = GameClient()->m_aClients[Own].m_RenderPos;
			TextRender()->TextColor(0.55f, 0.8f, 1.0f, 0.75f);
			TextRender()->Text(Position.x - TextRender()->TextWidth(11, aMessage) * 0.5f, Position.y - 92.0f, 11, aMessage);
			TextRender()->TextColor(PreviousColor);
		}
	}
	const auto ProjectileEnabled = [](int Weapon, bool Aim) {
		if(Weapon == WEAPON_GUN) return Aim ? g_Config.m_NullAimLineGun : g_Config.m_NullBulletLineGun;
		if(Weapon == WEAPON_SHOTGUN) return Aim ? g_Config.m_NullAimLineShotgun : g_Config.m_NullBulletLineShotgun;
		if(Weapon == WEAPON_GRENADE) return Aim ? g_Config.m_NullAimLineGrenade : g_Config.m_NullBulletLineGrenade;
		return 0;
	};
	const auto DrawPath = [&](const std::vector<vec2> &Points, ColorRGBA Head, ColorRGBA Tail, float StartWidth, float EndWidth) {
		for(size_t I = 1; I < Points.size(); ++I)
		{
			const vec2 A = Points[I - 1], B = Points[I], Delta = B - A;
			const float Length = length(Delta);
			if(Length < .001f)
				continue;
			const vec2 Normal(-Delta.y / Length, Delta.x / Length);
			const float T0 = static_cast<float>(I - 1) / (Points.size() - 1);
			const float T1 = static_cast<float>(I) / (Points.size() - 1);
			const vec2 NA = Normal * mix(StartWidth, EndWidth, T0), NB = Normal * mix(StartWidth, EndWidth, T1);
			const auto ColorAt = [&](float T) { return ColorRGBA(mix(Head.r, Tail.r, T), mix(Head.g, Tail.g, T), mix(Head.b, Tail.b, T), mix(Head.a, Tail.a, T)); };
			const ColorRGBA CA = ColorAt(T0), CB = ColorAt(T1);
			Graphics()->SetColor4(CA, CB, CA, CB);
			IGraphics::CFreeformItem Quad(A.x + NA.x, A.y + NA.y, B.x + NB.x, B.y + NB.y, A.x - NA.x, A.y - NA.y, B.x - NB.x, B.y - NB.y);
			Graphics()->QuadsDrawFreeform(&Quad, 1);
		}
	};
	if(g_Config.m_NullAimLines)
	{
		std::vector<CAimTee> Tees;
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			if(GameClient()->m_Snap.m_aCharacters[Id].m_Active)
				Tees.push_back({Id, GameClient()->m_aClients[Id].m_RenderPos});
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		for(const auto &Tee : Tees)
		{
			const int Id = Tee.m_Id;
			if(g_Config.m_NullAimLineSelfOnly && Id != Own)
				continue;
			const auto &Snap = GameClient()->m_Snap.m_aCharacters[Id].m_Cur;
			const int Weapon = Snap.m_Weapon;
			if(Weapon == WEAPON_LASER ? !g_Config.m_NullAimLineLaser : !ProjectileEnabled(Weapon, true))
				continue;
			auto *pCharacter = Source.GetCharacterById(Id);
			const int Zone = pCharacter ? pCharacter->GetPureTuneZone() : 0;
			const float Radius = pCharacter ? pCharacter->GetProximityRadius() : 28.0f;
			const vec2 Direction = Id == Own ? direction(angle(GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy])) : direction(Snap.m_Angle / 256.0f);
			const ColorRGBA Color = ProjectileColor(Weapon).WithAlpha(g_Config.m_NullAimLineAlpha / 100.0f);
			const auto Points = VisibleAimPath(Source, Tee.m_Position, Direction, Id, Weapon, Zone, Radius, g_Config.m_NullAimLineTicks, Tees);
			DrawPath(Points, Color, Color.WithAlpha(Color.a * .15f), 1, 1);
		}
		Graphics()->QuadsEnd();
	}
	if(g_Config.m_NullGodmodeShotPath)
	{
		const int Tick = Client()->PredGameTick(g_Config.m_ClDummy);
		if(Tick != m_ShotPathTick)
		{
			m_vShotPaths = ProjectilePaths(Source, g_Config.m_NullGodmodeShotPathTicks);
			m_ShotPathTick = Tick;
		}
		const float Alpha = g_Config.m_NullBulletLineAlpha / 100.0f;
		const float Pulse = std::sin(Client()->LocalTime() * 6.0f) * .5f + .5f;
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		for(const auto &Path : m_vShotPaths)
		{
			const bool OwnPath = Path.m_Owner == Own;
			if(!ProjectileEnabled(Path.m_Weapon, false) || (g_Config.m_NullBulletLineSelfOnly && !OwnPath) || Path.m_vPoints.size() < 2)
				continue;
			const ColorRGBA Head = OwnPath ? ColorRGBA(.4f, .9f, 1, Alpha) : ColorRGBA(1, .55f, .2f, Alpha);
			const ColorRGBA Tail = OwnPath ? ColorRGBA(.4f, .9f, 1, Alpha * .2f) : ColorRGBA(1, .3f, .2f, Alpha * .25f);
			DrawPath(Path.m_vPoints, Head, Tail, 2.5f, 1.0f);
			if(Path.m_Lost)
			{
				const float R = 7.0f + 3.0f * Pulse;
				const vec2 P = Path.m_vPoints.back();
				Graphics()->SetColor(OwnPath ? ColorRGBA(.5f, 1, 1, .5f + .3f * Pulse) : ColorRGBA(1, .35f, .2f, .5f + .35f * Pulse));
				IGraphics::CFreeformItem Diamond(P.x - R, P.y, P.x, P.y - R, P.x, P.y + R, P.x + R, P.y);
				Graphics()->QuadsDrawFreeform(&Diamond, 1);
			}
		}
		Graphics()->QuadsEnd();
	}
}
}
