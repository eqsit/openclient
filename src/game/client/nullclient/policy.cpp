#include "policy.h"
#include "route.h"

#include <game/client/krx/msvc_sort.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>

namespace OpenKrx::NullClient
{
	namespace
	{
		void Aim(CNetObj_PlayerInput &Input, vec2 Direction)
		{
			Input.m_TargetX = static_cast<int>(Direction.x * 100.0f);
			Input.m_TargetY = static_cast<int>(Direction.y * 100.0f);
			if(Input.m_TargetX == 0 && Input.m_TargetY == 0)
				Input.m_TargetX = 1;
		}
	}

	CNetObj_PlayerInput MoveInput(const CMove &Move, const CNetObj_PlayerInput &Base, int Step, bool HandBack)
	{
		auto Input = Base;
		Input.m_Jump = Step == 0 ? Move.m_Jump : 0;
		if(Move.m_WeaponAction >= 0)
		{
			if(Move.m_WantedWeapon >= 0)
				Input.m_WantedWeapon = Move.m_WantedWeapon + 1;
			Aim(Input, Move.m_WeaponAim);
			if(Step >= Move.m_FireAt)
				Input.m_Fire = Base.m_Fire + 2;
		}
		if(Move.m_Duration <= 0 || Step < Move.m_Duration)
		{
			Input.m_Direction = Move.m_ReverseAt > 0 && Step >= Move.m_ReverseAt ? -Move.m_Direction : Move.m_Direction;
			switch(Move.m_HookMode)
			{
			case 0: Input.m_Hook = 0; break;
			case 1: Input.m_Hook = 1; break;
			case 2:
				Input.m_Hook = Move.m_ReleaseHookFirst && Step == 0 ? 0 : 1;
				if(Move.m_ReleaseHookAt > 0 && Step >= Move.m_ReleaseHookAt)
					Input.m_Hook = 0;
				Aim(Input, Move.m_HookAim);
				break;
			}
		}
		else if(!HandBack)
		{
			Input.m_Direction = 0;
			Input.m_Hook = 0;
		}
		return Input;
	}

	float InputDifference(const CNetObj_PlayerInput &Input, const CNetObj_PlayerInput &Reference, float JumpCost, float ReleaseHookCost)
	{
		float Cost = Input.m_Direction == Reference.m_Direction ? 0.0f : Reference.m_Direction == 0 ? 0.25f :
													      0.5f;
		if((Input.m_Hook != 0) != (Reference.m_Hook != 0))
		{
			Cost += 1.0f;
			if(Reference.m_Hook != 0 && Input.m_Hook == 0)
				Cost += ReleaseHookCost;
		}
		if(Input.m_Jump != 0 && Reference.m_Jump == 0)
			Cost += JumpCost;
		if(Input.m_TargetX != Reference.m_TargetX || Input.m_TargetY != Reference.m_TargetY)
			Cost += 0.25f;
		return Cost;
	}

	CProbeResult Probe(IWorld &World, const CNetObj_PlayerInput &Input, int Ticks, bool FreezeFatal)
	{
		CProbeResult Result;
		for(int Step = 0; Step < Ticks; ++Step)
		{
			if(!World.Tick(Input) || World.Dead())
			{
				Result.m_Life = Step;
				Result.m_Died = true;
				Result.m_Speed = World.Valid() ? length(World.Velocity()) : 0;
				return Result;
			}
			if(FreezeFatal && World.Frozen())
			{
				Result.m_Life = Step;
				Result.m_Speed = length(World.Velocity());
				return Result;
			}
		}
		Result.m_Life = Ticks;
		Result.m_Speed = World.Valid() ? length(World.Velocity()) : 0;
		return Result;
	}

	std::vector<CMove> MovementCandidates(CCollision *pCollision, vec2 Position, float HookLength, int HookState, bool CanJump, int HookAngles, bool AllowHook, std::vector<vec2> *pHookPoints)
	{
		std::vector<CMove> vMoves;
		std::vector<vec2> vPoints;
		std::vector<vec2> vDirections;
		if(!pCollision)
			return vMoves;
		const int Angles = std::max(4, HookAngles);
		for(int Index = 0; Index < Angles; ++Index)
		{
			const float Angle = static_cast<float>(Index) * 6.2831855f / static_cast<float>(Angles);
			const vec2 Direction(std::cos(Angle), std::sin(Angle));
			vec2 Hit;
			if(!pCollision->IntersectLineTeleHook(Position, Position + Direction * HookLength, &Hit, nullptr))
				continue;
			if(std::any_of(vPoints.begin(), vPoints.end(), [&](vec2 Previous) { return distance(Previous, Hit) < 24.0f; }))
				continue;
			vPoints.push_back(Hit);
			vDirections.push_back(Direction);
		}
		if(pHookPoints)
			*pHookPoints = vPoints;
		const bool ExistingHook = HookState == 4 || HookState == 5;
		// Original constant table 1403ffd98; order affects stable equal-cost choices.
		for(int Duration : {1, 3, 5, 8, 0})
			for(int Direction = -1; Direction <= 1; ++Direction)
				for(int Jump = 0; Jump <= static_cast<int>(CanJump); ++Jump)
				{
					CMove Move;
					Move.m_Direction = Direction;
					Move.m_Jump = Jump;
					Move.m_Duration = Duration;
					vMoves.push_back(Move);
					if(ExistingHook)
					{
						Move.m_HookMode = 1;
						vMoves.push_back(Move);
					}
					if(Direction != 0 && Duration > 1)
					{
						std::vector<int> vReversals;
						for(int At : {1, Duration / 2, Duration - 1})
							if(At > 0 && At < Duration && std::find(vReversals.begin(), vReversals.end(), At) == vReversals.end())
								vReversals.push_back(At);
						for(int At : vReversals)
						{
							Move.m_ReverseAt = At;
							Move.m_HookMode = 0;
							vMoves.push_back(Move);
							if(ExistingHook)
							{
								Move.m_HookMode = 1;
								vMoves.push_back(Move);
							}
						}
					}
					Move.m_ReverseAt = 0;
					if(Duration == 8 && AllowHook)
						for(int ReleaseAt : {3, 6, 0})
							for(vec2 AimDirection : vDirections)
							{
								Move.m_HookMode = 2;
								Move.m_ReleaseHookFirst = HookState != 0;
								Move.m_ReleaseHookAt = ReleaseAt;
								Move.m_HookAim = AimDirection;
								vMoves.push_back(Move);
							}
				}
		return vMoves;
	}

	void AddWeaponCandidates(std::vector<CMove> &vMoves, const std::vector<vec2> &vHookPoints, vec2 Position, bool Grenade, std::optional<vec2> HammerTarget, bool CanJump, int ActiveWeapon, int Reload)
	{
		const auto Add = [&](int Weapon, vec2 AimDirection) {
			for(int Direction = -1; Direction <= 1; ++Direction)
				for(int Jump = 0; Jump <= static_cast<int>(CanJump); ++Jump)
				{
					CMove Move;
					Move.m_Direction = Direction;
					Move.m_Jump = Jump;
					Move.m_WeaponAction = Weapon;
					Move.m_WeaponAim = AimDirection;
					Move.m_WantedWeapon = Weapon == ActiveWeapon ? -1 : Weapon;
					Move.m_FireAt = Weapon == ActiveWeapon ? 0 : std::max(1, Reload);
					vMoves.push_back(Move);
				}
		};
		if(Grenade)
		{
			Add(3, vec2(0, 1));
			std::vector<std::pair<float, vec2>> vNearest;
			for(vec2 Point : vHookPoints)
			{
				const vec2 Delta = Point - Position;
				const float Distance = length(Delta);
				if(Distance >= 0.001f)
					vNearest.emplace_back(Distance, Delta / Distance);
			}
			MsvcSort(vNearest.begin(), vNearest.end(), static_cast<ptrdiff_t>(vNearest.size()), [](const auto &Left, const auto &Right) { return Left.first < Right.first; });
			for(size_t Index = 0; Index < std::min<size_t>(8, vNearest.size()); ++Index)
				Add(3, vNearest[Index].second);
		}
		if(HammerTarget && distance(*HammerTarget, Position) >= 0.001f)
			Add(0, normalize(*HammerTarget - Position));
	}

	CEvaluation Evaluate(IWorld &World, const CMove &Move, const CNetObj_PlayerInput &Base, const CWeights &Weights, int Ticks, float CostLimit)
	{
		CEvaluation Result;
		if(!World.Valid())
		{
			Result.m_FailureTick = 0;
			Result.m_SafeEnd = false;
			return Result;
		}
		const bool Grounded = World.Grounded();
		float Cost = 0;
		if(Move.m_Jump != 0)
			Cost = Grounded ? (Weights.m_GroundJump > 0 ? Weights.m_GroundJump : 40.0f) : (Weights.m_AirJump > 0 ? Weights.m_AirJump : 900.0f);
		int Commitment = Move.m_Duration == 0 ? Ticks : Move.m_Duration;
		if(Move.m_HookMode == 2)
		{
			Cost += Weights.m_HookCost;
			Commitment = Move.m_ReleaseHookAt > 0 ? Move.m_ReleaseHookAt : Ticks;
		}
		Cost = static_cast<float>(Commitment) * Weights.m_Commitment + Cost;
		const auto First = MoveInput(Move, Base, 0, Weights.m_HandBack);
		if(Weights.m_pManual)
		{
			const float Difference = InputDifference(First, *Weights.m_pManual, Grounded ? 0.75f : 1.5f, Weights.m_ReleaseHookCost);
			if(Weights.m_Deviation > 0)
				Result.m_ManualDifference = static_cast<int>(Difference * 4.0f + 0.5f);
			Cost = Difference * Weights.m_Deviation + Cost;
		}
		if(Weights.m_pPrevious)
		{
			const float Difference = InputDifference(First, *Weights.m_pPrevious, 0.75f, 0);
			Result.m_PreviousDifference = static_cast<int>(Difference * 4.0f + 0.5f);
			Cost += Difference * Weights.m_Continuity;
		}
		if(First.m_Hook != 0)
			Cost += static_cast<float>(Weights.m_HookAge) * Weights.m_HookAgeCost;
		int FrozenTicks = 0, HammerTicks = 0, RocketTicks = 0, ClearanceTicks = 0, WallTicks = 0;
		bool DeepFrozen = false;
		for(int Step = 0; Step < Ticks; ++Step)
		{
			const auto Input = MoveInput(Move, Base, Step, Weights.m_HandBack);
			if(!World.Tick(Input) || World.Dead())
			{
				Result.m_FailureTick = Step;
				Result.m_SafeEnd = false;
				Result.m_Cost = Cost;
				return Result;
			}
			if(World.Frozen())
			{
				Result.m_SafeEnd = false;
				if(Weights.m_FreezeFatal)
				{
					Result.m_FailureTick = Step;
					Result.m_Cost = Cost;
					return Result;
				}
				++FrozenTicks;
				DeepFrozen |= World.DeepFrozen();
			}
			HammerTicks += Weights.m_HammerCost > 0 && World.HammerDanger();
			RocketTicks += Weights.m_RocketCost > 0 && World.RocketDanger(Weights.m_RocketThrow, Weights.m_RocketSpread);
			ClearanceTicks += Weights.m_Clearance > 0 && Weights.m_FreezeCost > 0 && World.NearFreeze(Weights.m_Clearance);
			WallTicks += World.WallStopped();
			const float Distance = std::max(0.0f, distance(World.Position(), Weights.m_Anchor) - Weights.m_HoldSlack);
			Cost += Distance * ((static_cast<float>(Step + 1) / static_cast<float>(Ticks)) * 0.75f + 0.25f) * Weights.m_Hold;
			if(CostLimit > 0 && Cost >= CostLimit)
			{
				Result.m_Pruned = true;
				Result.m_Cost = Cost;
				return Result;
			}
		}
		const vec2 Position = World.Position();
		const vec2 Velocity = World.Velocity();
		const bool EndGrounded = World.Grounded();
		const float Distance = distance(Position, Weights.m_Anchor);
		if(Weights.m_pReservedFlyPath)
		{
			float RouteDistance;
			const float Progress = Weights.m_pReservedFlyPath->Progress(Position, RouteDistance);
			Cost = (Cost - Progress * 12.0f) + RouteDistance * 3.0f;
		}
		else
		{
			Cost += std::max(0.0f, Distance - Weights.m_HoldSlack) * 6.0f * Weights.m_Hold;
			if(Weights.m_Settle <= 0 && Distance < 60.0f)
				Cost = length(Velocity) * 20.0f * Weights.m_Hold + Cost;
		}
		if(Weights.m_CounterPull > 0)
		{
			const float Delta = Weights.m_CounterBelow ? Weights.m_CounterY - Position.y : Position.y - Weights.m_CounterY;
			if(Delta > 0)
				Cost = Weights.m_CounterPull * Delta + Cost;
		}
		if(Weights.m_Travel > 0)
			Cost = std::max(0.0f, distance(Position, Weights.m_Start) - 32.0f) * Weights.m_Travel + Cost;
		if(Weights.m_Coast > 0 && Result.m_SafeEnd && (!EndGrounded || length(Velocity) >= 8.0f))
		{
			auto Input = MoveInput(Move, Base, Ticks, Weights.m_HandBack);
			Input.m_Direction = Input.m_Jump = Input.m_Hook = 0;
			for(int Step = 0; Step < Weights.m_Coast; ++Step)
				if(!World.Tick(Input) || World.Dead() || World.Frozen())
				{
					Result.m_SafeEnd = false;
					break;
				}
		}
		if(Weights.m_Settle > 0 && Distance > 0.001f)
		{
			const float Away = dot((Position - Weights.m_Anchor) / Distance, Velocity);
			Cost = std::max(0.0f, Away) * Weights.m_Settle + Cost;
		}
		Cost = static_cast<float>(FrozenTicks) * Weights.m_FreezeCost * (DeepFrozen ? 40.0f : 1.0f) + Cost +
		       static_cast<float>(ClearanceTicks) * Weights.m_FreezeCost * 0.25f +
		       static_cast<float>(RocketTicks) * Weights.m_RocketCost + static_cast<float>(HammerTicks) * Weights.m_HammerCost +
		       static_cast<float>(WallTicks) * Weights.m_WallCost;
		if(World.HookedPlayer())
			Cost += Weights.m_TeeHookCost;
		const float Hazard = World.HazardRay(Position, Velocity);
		if(Hazard > 0)
		{
			const float Scale = Weights.m_HazardSpeed > 0 ? std::min(1.0f, length(Velocity) / Weights.m_HazardSpeed) : 1.0f;
			if(Scale >= 1.0f && Weights.m_Coast <= 0)
				Result.m_SafeEnd = false;
			Cost += Scale * Hazard * Scale;
		}
		if(Weights.m_FreezeFatal && !Result.m_SafeEnd)
			Result.m_FailureTick = Ticks;
		Result.m_Cost = Cost;
		return Result;
	}

	CNetObj_PlayerInput CPlanner::FollowInput(const CNetObj_PlayerInput &Base, int Step) const
	{
		return MoveInput(m_LastMove, Base, Step, m_LastHandBack);
	}

	bool CPlanner::CanFollow(IWorld &World, const CNetObj_PlayerInput &Base, int Start, int Ticks, bool FreezeFatal) const
	{
		if(!m_HasLast)
			return false;
		World.Reset();
		for(int Step = 0; Step < Ticks; ++Step)
			if(!World.Tick(MoveInput(m_LastMove, Base, Start + Step, m_LastHandBack)) || World.Dead() || (FreezeFatal && World.Frozen()))
				return false;
		return true;
	}

	bool CPlanner::HasEscape(IWorld &World, const CNetObj_PlayerInput &Base, const CWeights &Weights, int Ticks)
	{
		if(Weights.m_Chain > 0)
			return Select(World, Base, Weights, Ticks).m_Found;
		if(m_LastSafeIndex >= 0 && m_LastSafeIndex < static_cast<int>(m_vMoves.size()))
		{
			World.Reset();
			if(Evaluate(World, m_vMoves[m_LastSafeIndex], Base, Weights, Ticks).m_FailureTick < 0)
				return true;
		}
		for(size_t Index = 0; Index < m_vMoves.size(); ++Index)
		{
			if(static_cast<int>(Index) == m_LastSafeIndex)
				continue;
			World.Reset();
			if(Evaluate(World, m_vMoves[Index], Base, Weights, Ticks).m_FailureTick < 0)
			{
				m_LastSafeIndex = static_cast<int>(Index);
				return true;
			}
		}
		m_LastSafeIndex = -1;
		return false;
	}

	int CPlanner::Chain(IWorld &World, const CNetObj_PlayerInput &Base, const CWeights &Weights, int Ticks)
	{
		std::vector<int> vPrefixes;
		for(size_t Index = 0; Index < m_vEvaluations.size(); ++Index)
			if(m_vEvaluations[Index].m_FailureTick < 0)
				vPrefixes.push_back(static_cast<int>(Index));
		MsvcSort(vPrefixes.begin(), vPrefixes.end(), static_cast<ptrdiff_t>(vPrefixes.size()), [&](int Left, int Right) { return m_vEvaluations[Left].m_Cost < m_vEvaluations[Right].m_Cost; });
		if(vPrefixes.size() > static_cast<size_t>(Weights.m_Chain))
			vPrefixes.resize(Weights.m_Chain);
		CWeights FollowWeights = Weights;
		FollowWeights.m_pManual = nullptr;
		FollowWeights.m_pPrevious = nullptr;
		FollowWeights.m_Chain = 0;
		int Best = -1;
		bool BestStable = false;
		float BestCost = std::numeric_limits<float>::max();
		for(int Prefix : vPrefixes)
		{
			World.Reset();
			bool ValidPrefix = true;
			for(int Step = 0; Step < Ticks; ++Step)
				if(!World.Tick(MoveInput(m_vMoves[Prefix], Base, Step, Weights.m_HandBack)) || World.Dead())
				{
					ValidPrefix = false;
					break;
				}
			if(!ValidPrefix)
				continue;
			// Original checkpoint0 retains the state after the prefix. A clone is
			// equivalent and leaves the caller's immutable root available to Reset.
			const auto pAfterPrefix = World.Clone();
			for(const auto &Move : m_vMoves)
			{
				if((Move.m_HookMode == 2 && Move.m_ReleaseHookAt != 3) || Move.m_WeaponAction >= 0)
					continue;
				auto pSuffix = pAfterPrefix->Clone();
				const auto Evaluation = Evaluate(*pSuffix, Move, Base, FollowWeights, Ticks);
				if(Evaluation.m_FailureTick < 0 &&
					((Evaluation.m_SafeEnd && !BestStable) || (Evaluation.m_SafeEnd == BestStable && Evaluation.m_Cost < BestCost)))
				{
					Best = Prefix;
					BestCost = Evaluation.m_Cost;
					BestStable = Evaluation.m_SafeEnd;
				}
			}
		}
		return BestStable ? Best : -1;
	}

	CSelection CPlanner::Select(IWorld &World, const CNetObj_PlayerInput &Base, const CWeights &Weights, int Ticks)
	{
		CSelection Selection;
		if(m_vMoves.empty())
			return Selection;
		std::vector<int> vOrder;
		std::vector<int> vKeys(m_vMoves.size());
		World.Reset();
		const float JumpCost = World.Grounded() ? 0.75f : 1.5f;
		for(size_t Index = 0; Index < m_vMoves.size(); ++Index)
		{
			const auto Input = MoveInput(m_vMoves[Index], Base, 0, Weights.m_HandBack);
			int ManualDifference = 0, PreviousDifference = 0;
			if(Weights.m_pManual && Weights.m_Deviation > 0)
				ManualDifference = static_cast<int>(InputDifference(Input, *Weights.m_pManual, JumpCost, Weights.m_ReleaseHookCost) * 4.0f + 0.5f);
			if(Weights.m_pPrevious)
				PreviousDifference = static_cast<int>(InputDifference(Input, *Weights.m_pPrevious, 0.75f, 0) * 4.0f + 0.5f);
			vKeys[Index] = ManualDifference * 4096 + std::min(4095, PreviousDifference);
			const auto *pManual = Weights.m_pManual;
			if(!Weights.m_RespectHeld || !pManual ||
				((pManual->m_Direction == 0 || Input.m_Direction == pManual->m_Direction) &&
					(pManual->m_Jump == 0 || Input.m_Jump != 0) &&
					(pManual->m_Hook == 0 || (Input.m_Hook != 0 && Input.m_TargetX == pManual->m_TargetX && Input.m_TargetY == pManual->m_TargetY))))
				vOrder.push_back(static_cast<int>(Index));
		}
		std::stable_sort(vOrder.begin(), vOrder.end(), [&](int Left, int Right) { return vKeys[Left] < vKeys[Right]; });
		// Null initializes all entries before applying the held-input filter.
		m_vEvaluations.assign(m_vMoves.size(), CEvaluation{});
		World.ForEachCandidate(static_cast<int>(vOrder.size()), [&](IWorld &Private, int OrderIndex) {
			const int Index = vOrder[OrderIndex];
			Private.Reset();
			m_vEvaluations[Index] = Evaluate(Private, m_vMoves[Index], Base, Weights, Ticks);
		});
		int Best = -1;
		for(int Index : vOrder)
		{
			const auto &Evaluation = m_vEvaluations[Index];
			if(Evaluation.m_FailureTick < 0 && Evaluation.m_SafeEnd &&
				(Best < 0 || std::tie(Evaluation.m_ManualDifference, Evaluation.m_PreviousDifference, Evaluation.m_Cost) <
						     std::tie(m_vEvaluations[Best].m_ManualDifference, m_vEvaluations[Best].m_PreviousDifference, m_vEvaluations[Best].m_Cost)))
				Best = Index;
		}
		Selection.m_StableEnd = Best >= 0;
		Selection.m_Found = Best >= 0;
		if(Best < 0)
		{
			int BestFailed = -1;
			for(size_t Index = 0; Index < m_vEvaluations.size(); ++Index)
			{
				const auto &Evaluation = m_vEvaluations[Index];
				if(Evaluation.m_FailureTick < 0)
				{
					if(Best < 0 || Evaluation.m_Cost < m_vEvaluations[Best].m_Cost)
						Best = static_cast<int>(Index);
				}
				else if(BestFailed < 0 || Evaluation.m_FailureTick > m_vEvaluations[BestFailed].m_FailureTick ||
					(Evaluation.m_FailureTick == m_vEvaluations[BestFailed].m_FailureTick && Evaluation.m_Cost < m_vEvaluations[BestFailed].m_Cost))
					BestFailed = static_cast<int>(Index);
			}
			if(Weights.m_Chain > 0)
			{
				const int Chained = Chain(World, Base, Weights, Ticks);
				if(Chained >= 0)
				{
					Best = Chained;
					Selection.m_Chained = Selection.m_StableEnd = true;
				}
			}
			Selection.m_Found = Best >= 0;
			if(Best < 0)
				Best = BestFailed;
		}
		if(Best < 0)
			return Selection;
		const auto &Move = m_vMoves[Best];
		Selection.m_Input = MoveInput(Move, Base, 0, Weights.m_HandBack);
		Selection.m_Move = Best;
		Selection.m_Weapon = Move.m_WeaponAction;
		if(Move.m_HookMode == 1)
			Selection.m_HookHold = Move.m_Duration > 0 ? Move.m_Duration : Ticks;
		else if(Move.m_HookMode == 2)
			Selection.m_HookHold = Move.m_ReleaseHookAt > 0 ? Move.m_ReleaseHookAt : Ticks;
		m_LastMove = Move;
		m_LastHandBack = Weights.m_HandBack;
		m_HasLast = true;
		m_LastWindow = Move.m_Duration > 0 ? Ticks : 0;
		return Selection;
	}
}
