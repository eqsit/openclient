#include "fly.h"
#include "simulation.h"
#include <base/log.h>
#include <base/time.h>
#include <engine/shared/client_profile.h>
#include <engine/keys.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <algorithm>
namespace OpenKrx::NullClient
{
bool CFly::Active() const { return NullProfileActive() && (g_Config.m_NullFlymode || m_Hold); }
bool CFly::OnInput(CGameClient *pClient, const IInput::CEvent &Event)
{
	if(g_Config.m_NullBindFlyhold && Event.m_Key == g_Config.m_NullBindFlyhold)
	{
		m_Hold = (Event.m_Flags & IInput::FLAG_PRESS) != 0;
		// 1401fc670 arms both held flight and mouse steering.
		m_Drag = m_Hold;
		return true;
	}
	// Release events must clear latched directions even after flight was disabled.
	if(!Active() && !(Event.m_Flags & IInput::FLAG_RELEASE)) return false;
	const bool Press = (Event.m_Flags & IInput::FLAG_PRESS) != 0;
	const int aKeys[] = {KEY_W, KEY_A, KEY_S, KEY_D};
	for(int i = 0; i < 4; ++i)
		if(Event.m_Key == aKeys[i]) { m_aDirections[i] = Press; return true; }
	if(Event.m_Key == KEY_MOUSE_1) { m_Drag = Press; return true; }
	return false;
}
bool CFly::OnCursorMove(CGameClient *pClient, float X, float Y)
{
	if(!Active() || !(m_Drag || m_Hold) || !m_ValidMarker) return false;
	m_Marker += vec2(X, Y) * pClient->m_Camera.m_Zoom;
	return true;
}
void CFly::Update(CGameClient *pClient)
{
	if(!Active())
	{
		m_HasPlan = m_ValidMarker = m_Drag = false;
		std::fill(std::begin(m_aDirections), std::end(m_aDirections), false);
		return;
	}
	if(pClient->Client()->State() != IClient::STATE_ONLINE || pClient->m_Snap.m_SpecInfo.m_Active) { m_HasPlan = false; return; }
	const int Slot = g_Config.m_ClDummy, Id = pClient->m_aLocalIds[Slot];
	auto *pCharacter = pClient->KrxPredictedWorld().GetCharacterById(Id);
	if(!pCharacter) { m_HasPlan = false; return; }
	if(!m_ValidMarker || m_Slot != Slot) { m_Marker = pCharacter->GetPos(); m_ValidMarker = true; m_Route.Reset(); m_LastTick = -1; }
	m_Slot = Slot;
	vec2 Movement(static_cast<float>(m_aDirections[3]) - m_aDirections[1], static_cast<float>(m_aDirections[2]) - m_aDirections[0]);
	if(length(Movement) > 0) m_Marker += normalize(Movement) * g_Config.m_NullFlymodeSpeed * pClient->Client()->RenderFrameTime();
	const int Tick = pClient->Client()->GameTick(Slot);
	if(Tick == m_LastTick) return;
	m_LastTick = Tick; m_HasPlan = false;
	const vec2 Position = pCharacter->GetPos();
	auto *pCollision = pClient->Collision();
	m_Goal = m_Marker;
	const bool AvoidFreeze = g_Config.m_NullFlymodePathAvoidFreeze != 0;
	if(!g_Config.m_NullFlymodePathfinder) m_Route.Clear();
	else
	{
		const bool Direct = CRoute::SegmentSafe(pCollision, Position, m_Marker, AvoidFreeze);
		if(Direct && distance(Position, m_Marker) < 320) m_Route.Clear();
		else if(!Direct || m_Route.Valid())
		{
			float Dist = 0;
			if(m_Route.Valid()) { m_Route.UpdateCursor(Position); m_Route.Progress(Position, Dist); }
			if(!m_Route.Valid() || distance(m_LastRouteMarker, m_Marker) > 64 || Dist > 260 || Tick - m_LastRouteTick >= g_Config.m_NullFlymodePathInterval * 20)
			{
				m_LastRouteTick = Tick; m_LastRouteMarker = m_Marker;
				if(m_Route.Build(pCollision, Position, m_Marker, g_Config.m_NullFlymodePathNodes, AvoidFreeze)) m_Route.UpdateCursor(Position);
			}
			if(m_Route.Valid()) m_Goal = m_Route.Goal(pCollision, Position, m_Marker, 420, AvoidFreeze);
		}
	}
	CSettings Settings = SettingsFromConfig();
	Settings.m_Horizon = 1000; // radius is not horizon-clipped in original Fly.
	Settings.m_SimRadius = g_Config.m_NullFlymodeSimRadius;
	Settings.m_PlayerRadius = Settings.m_WeaponRadius = 0;
	Settings.m_Adversarial = 0;
	CSimulation World;
	if(!World.Initialize(pClient->KrxPredictedWorld(), Id, Settings, {})) return;
	const auto *pCore = pCharacter->Core();
	m_Planner.SetCandidates(MovementCandidates(pCollision, Position, pCore->m_Tuning.m_HookLength, pCore->m_HookState,
		pCharacter->IsGrounded() || !(pCore->m_Jumped & 2), g_Config.m_NullFlymodeHookAngles, true));
	if(m_Planner.Candidates().empty()) return;
	CWeights Weights;
	Weights.m_Anchor = m_Goal; Weights.m_Hold = 1;
	Weights.m_pReservedFlyPath = m_Route.Valid() ? &m_Route : nullptr;
	auto Raw = pClient->m_Controls.m_aInputData[Slot];
	const int64_t Start = time_get_impl();
	const uint64_t StepsBefore = World.TotalSteps();
	const auto Selection = m_Planner.Select(World, Raw, Weights, g_Config.m_NullFlymodeHorizon);
	const int64_t Duration = time_get_impl() - Start;
	m_Planned = Selection.m_Input;
	m_HasPlan = true; // The original also emits its best-effort choice.
	if(g_Config.m_NullFlymodeProfile)
	{
		++m_ProfileCount; m_ProfileFailed += !Selection.m_Found;
		m_ProfileSteps += World.TotalSteps() - StepsBefore; m_ProfileTime += Duration; m_ProfileWorst = std::max(m_ProfileWorst, Duration);
		if(m_ProfileCount >= 50)
		{
			log_info("null/fly", "plan %.2f ms avg, %.2f ms worst, %d ticks avg, %.2f us/tick, %d moves, %d/%d found nothing alive",
				1000.0 * m_ProfileTime / time_freq() / m_ProfileCount, 1000.0 * m_ProfileWorst / time_freq(), static_cast<int>(m_ProfileSteps / m_ProfileCount),
				m_ProfileSteps ? 1000000.0 * m_ProfileTime / time_freq() / m_ProfileSteps : 0, static_cast<int>(m_Planner.Candidates().size()), m_ProfileFailed, m_ProfileCount);
			m_ProfileCount = m_ProfileSteps = m_ProfileFailed = 0; m_ProfileTime = m_ProfileWorst = 0;
		}
	}
	else { m_ProfileCount = m_ProfileSteps = m_ProfileFailed = 0; m_ProfileTime = m_ProfileWorst = 0; }
}
CNetObj_PlayerInput CFly::Apply(CNetObj_PlayerInput Input, int Slot) const
{
	if(!HasPlan(Slot)) return Input;
	Input.m_Direction = m_Planned.m_Direction; Input.m_Jump = m_Planned.m_Jump;
	Input.m_Hook = m_Planned.m_Hook; Input.m_TargetX = m_Planned.m_TargetX; Input.m_TargetY = m_Planned.m_TargetY;
	return Input;
}
}
