#include "route.h"
#include <game/collision.h>
#include <game/mapitems.h>
#include <base/math.h>
#include <algorithm>
#include <cmath>
#include <limits>
namespace OpenKrx::NullClient
{
namespace
{
bool Hazard(CCollision *pCollision, vec2 P, bool AvoidFreeze)
{
	const int Index = pCollision->GetPureMapIndex(P);
	const int Game = pCollision->GetTileIndex(Index), Front = pCollision->GetFrontTileIndex(Index);
	return Game == TILE_DEATH || Front == TILE_DEATH || (AvoidFreeze &&
		(Game == TILE_FREEZE || Front == TILE_FREEZE || Game == TILE_DFREEZE || Front == TILE_DFREEZE));
}
struct CNode { int m_Index; float m_Cost; };
// MSVC pop_heap chooses the right child on equality. Equal scores can change
// the whole route; use its hole-then-sift algorithm on every platform.
void Push(std::vector<CNode> &Heap, CNode Value)
{
	size_t Hole = Heap.size(); Heap.push_back(Value);
	while(Hole > 0)
	{
		const size_t Parent = (Hole - 1) / 2;
		if(Heap[Parent].m_Cost <= Value.m_Cost) break;
		Heap[Hole] = Heap[Parent]; Hole = Parent;
	}
	Heap[Hole] = Value;
}
CNode Pop(std::vector<CNode> &Heap)
{
	const CNode Result = Heap.front(), Value = Heap.back();
	Heap.pop_back();
	if(Heap.empty()) return Result;
	const size_t Last = Heap.size() - 1, Bottom = Last / 2;
	size_t Hole = 0;
	while(Hole < Bottom)
	{
		const size_t Left = Hole * 2 + 1, Right = Left + 1;
		const size_t Child = Heap[Right].m_Cost <= Heap[Left].m_Cost ? Right : Left;
		Heap[Hole] = Heap[Child]; Hole = Child;
	}
	if(Hole == Bottom && Heap.size() % 2 == 0)
	{
		Heap[Hole] = Heap[Last]; Hole = Last;
	}
	while(Hole > 0)
	{
		const size_t Parent = (Hole - 1) / 2;
		if(Heap[Parent].m_Cost <= Value.m_Cost) break;
		Heap[Hole] = Heap[Parent]; Hole = Parent;
	}
	Heap[Hole] = Value;
	return Result;
}
}
bool CRoute::SegmentSafe(CCollision *pCollision, vec2 Start, vec2 End, bool AvoidFreeze)
{
	if(!pCollision || pCollision->IntersectLine(Start, End, nullptr, nullptr)) return false;
	const int Steps = std::max(1, static_cast<int>(distance(Start, End) * .0625f));
	for(int i = 0; i <= Steps; ++i)
		if(Hazard(pCollision, mix(Start, End, static_cast<float>(i) / Steps), AvoidFreeze)) return false;
	return true;
}
bool CRoute::Walkable(int X, int Y)
{
	if(X < 0 || Y < 0 || X >= m_Width || Y >= m_Height) return false;
	auto &Value = m_vWalkable[Y * m_Width + X];
	if(Value < 0)
	{
		const vec2 Center(X * 32.f + 16, Y * 32.f + 16);
		Value = !m_pCollision->CheckPoint(Center + vec2(-14, -14)) && !m_pCollision->CheckPoint(Center + vec2(14, -14)) &&
			!m_pCollision->CheckPoint(Center + vec2(-14, 14)) && !m_pCollision->CheckPoint(Center + vec2(14, 14)) &&
			!Hazard(m_pCollision, Center, m_AvoidFreeze);
	}
	return Value != 0;
}
bool CRoute::NearSurface(int X, int Y)
{
	auto &Value = m_vNearSurface[Y * m_Width + X];
	if(Value < 0)
	{
		Value = 0;
		for(int Radius = 1; Radius <= 7; ++Radius)
			for(int Dx = -1; Dx <= 1; ++Dx)
				for(int Dy = -1; Dy <= 1; ++Dy)
				{
					if(Dx == 0 && Dy == 0) continue;
					const int Tx = X + Radius * Dx, Ty = Y + Radius * Dy;
					if(Tx >= 0 && Ty >= 0 && Tx < m_Width && Ty < m_Height && m_pCollision->GetCollisionAt(Tx * 32 + 16, Ty * 32 + 16))
						return Value = 1;
				}
	}
	return Value != 0;
}
bool CRoute::Build(CCollision *pCollision, vec2 Start, vec2 End, int NodeLimit, bool AvoidFreeze)
{
	Clear();
	if(!pCollision) return false;
	const int Width = pCollision->GetWidth(), Height = pCollision->GetHeight();
	if(m_pCollision != pCollision || Width != m_Width || Height != m_Height || AvoidFreeze != m_AvoidFreeze)
	{
		m_pCollision = pCollision; m_Width = Width; m_Height = Height; m_AvoidFreeze = AvoidFreeze;
		m_vWalkable.assign(Width * Height, -1); m_vNearSurface.assign(Width * Height, -1);
	}
	const int Sx = round_to_int(Start.x) / 32, Sy = round_to_int(Start.y) / 32;
	const int Ex = round_to_int(End.x) / 32, Ey = round_to_int(End.y) / 32;
	if(!Walkable(Sx, Sy) || !Walkable(Ex, Ey)) return false;
	const int First = Sy * Width + Sx, Last = Ey * Width + Ex;
	std::vector<float> Costs(Width * Height, std::numeric_limits<float>::max());
	std::vector<int> Parents(Width * Height, -1);
	std::vector<bool> Closed(Width * Height, false);
	const auto Heuristic = [&](int X, int Y) {
		const float Dx = std::abs(static_cast<float>(X - Ex)), Dy = std::abs(static_cast<float>(Y - Ey));
		return (std::min(Dx, Dy) * .414f + std::max(Dx, Dy)) * .6f;
	};
	std::vector<CNode> Heap;
	Costs[First] = 0; Push(Heap, {First, Heuristic(Sx, Sy)});
	int Expanded = 0;
	while(!Heap.empty())
	{
		const int Index = Pop(Heap).m_Index;
		if(Closed[Index]) continue;
		Closed[Index] = true;
		if(Index == Last)
		{
			for(int At = Last; At >= 0; At = Parents[At]) m_vPoints.emplace_back((At % Width) * 32.f + 16, (At / Width) * 32.f + 16);
			std::reverse(m_vPoints.begin(), m_vPoints.end());
			float Progress = 0;
			for(size_t i = 0; i < m_vPoints.size(); ++i)
			{
				if(i) Progress += distance(m_vPoints[i - 1], m_vPoints[i]);
				m_vProgress.push_back(Progress);
			}
			return true;
		}
		if(++Expanded > NodeLimit) break;
		const int X = Index % Width, Y = Index / Width;
		for(int Dx = -1; Dx <= 1; ++Dx)
			for(int Dy = -1; Dy <= 1; ++Dy)
			{
				if(!Dx && !Dy) continue;
				const int Nx = X + Dx, Ny = Y + Dy;
				if(!Walkable(Nx, Ny) || (Dx && Dy && (!Walkable(Nx, Y) || !Walkable(X, Ny)))) continue;
				float Step = Dx && Dy ? 1.414f : 1.f;
				if(Dy > 0) Step *= .6f;
				else if(Dy < 0) Step *= NearSurface(Nx, Ny) ? 3.f : 9.f;
				else if(Ny + 1 >= Height || !pCollision->GetCollisionAt(Nx * 32 + 16, (Ny + 1) * 32 + 16)) Step *= NearSurface(Nx, Ny) ? .7f : 1.5f;
				const int Next = Ny * Width + Nx;
				const float Cost = Costs[Index] + Step;
				if(Cost < Costs[Next])
				{
					Costs[Next] = Cost; Parents[Next] = Index;
					Push(Heap, {Next, Heuristic(Nx, Ny) + Cost});
				}
			}
	}
	return false;
}
int CRoute::Nearest(vec2 Position, bool Initial) const
{
	int Result = m_Cursor;
	float Best = std::numeric_limits<float>::max();
	const int Start = Initial ? 0 : std::max(0, m_Cursor - 48);
	const int End = Initial ? static_cast<int>(m_vPoints.size()) - 1 : std::min(static_cast<int>(m_vPoints.size()) - 1, m_Cursor + 48);
	for(int i = Start; i <= End; ++i)
	{
		const float Dist = distance(Position, m_vPoints[i]);
		if(Dist < Best) { Best = Dist; Result = i; }
	}
	return Result;
}
float CRoute::Progress(vec2 Position, float &Distance) const
{
	if(!Valid()) { Distance = 0; return 0; }
	const int Index = Nearest(Position, false);
	Distance = distance(Position, m_vPoints[Index]);
	return m_vProgress[Index];
}
vec2 CRoute::Goal(CCollision *pCollision, vec2 Position, vec2 End, float LookAhead, bool AvoidFreeze) const
{
	if(!Valid() || !pCollision) return End;
	for(auto It = m_vPoints.rbegin(); It != m_vPoints.rend(); ++It)
		if(distance(Position, *It) <= LookAhead && SegmentSafe(pCollision, Position, *It, AvoidFreeze)) return *It;
	return m_vPoints.front();
}
}
