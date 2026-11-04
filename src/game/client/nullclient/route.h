#ifndef GAME_CLIENT_NULLCLIENT_ROUTE_H
#define GAME_CLIENT_NULLCLIENT_ROUTE_H
#include <base/vmath.h>
#include <vector>
#include <cstdint>
class CCollision;
namespace OpenKrx::NullClient
{
// Null 1401fd240..1401feaC0. Shared by Fly and Serial, never KRX.
class CRoute
{
	CCollision *m_pCollision = nullptr;
	int m_Width = 0, m_Height = 0;
	bool m_AvoidFreeze = false;
	std::vector<int8_t> m_vWalkable, m_vNearSurface;
	bool Walkable(int X, int Y);
	bool NearSurface(int X, int Y);
	int Nearest(vec2 Position, bool Initial) const;
public:
	std::vector<vec2> m_vPoints;
	std::vector<float> m_vProgress;
	int m_Cursor = 0;
	void Clear() { m_vPoints.clear(); m_vProgress.clear(); m_Cursor = 0; }
	void Reset() { *this = {}; }
	bool Build(CCollision *pCollision, vec2 Start, vec2 End, int NodeLimit, bool AvoidFreeze);
	bool Valid() const { return !m_vPoints.empty(); }
	void UpdateCursor(vec2 Position) { m_Cursor = Nearest(Position, m_Cursor < 1); }
	float Progress(vec2 Position, float &Distance) const;
	vec2 Goal(CCollision *pCollision, vec2 Position, vec2 End, float LookAhead, bool AvoidFreeze) const;
	static bool SegmentSafe(CCollision *pCollision, vec2 Start, vec2 End, bool AvoidFreeze);
};
}
#endif
