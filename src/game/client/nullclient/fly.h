#ifndef GAME_CLIENT_NULLCLIENT_FLY_H
#define GAME_CLIENT_NULLCLIENT_FLY_H
#include "route.h"
#include "policy.h"
#include <engine/input.h>
class CGameClient;
namespace OpenKrx::NullClient
{
class CFly
{
	CRoute m_Route;
	CPlanner m_Planner;
	vec2 m_Marker = vec2(0, 0), m_Goal = vec2(0, 0), m_LastRouteMarker = vec2(0, 0);
	bool m_ValidMarker = false, m_Hold = false, m_Drag = false, m_aDirections[4]{};
	bool m_HasPlan = false;
	int m_LastTick = -1, m_LastRouteTick = -1, m_Slot = -1;
	CNetObj_PlayerInput m_Planned{};
	int m_ProfileCount = 0, m_ProfileSteps = 0, m_ProfileFailed = 0;
	int64_t m_ProfileTime = 0, m_ProfileWorst = 0;
public:
	void Reset() { *this = {}; }
	bool Active() const;
	bool HasPlan(int Slot) const { return m_HasPlan && Slot == m_Slot && Active(); }
	bool OnInput(CGameClient *pClient, const IInput::CEvent &Event);
	bool OnCursorMove(CGameClient *pClient, float X, float Y);
	void Update(CGameClient *pClient);
	CNetObj_PlayerInput Apply(CNetObj_PlayerInput Input, int Slot) const;
	bool HasMarker() const { return m_ValidMarker && Active(); }
	vec2 Marker() const { return m_Marker; }
	vec2 Goal() const { return m_Goal; }
	const CRoute &Route() const { return m_Route; }
};
}
#endif
