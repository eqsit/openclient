#ifndef GAME_CLIENT_NULLCLIENT_REPORT_ANALYSIS_H
#define GAME_CLIENT_NULLCLIENT_REPORT_ANALYSIS_H

#include <base/vmath.h>

#include <algorithm>
#include <array>
#include <deque>

namespace OpenKrx::NullClient
{
	struct CAutopsyFrame
	{
		struct CTee
		{
			int m_Id, m_Freeze, m_Hooked;
			float m_Distance;
		};
		struct CShell
		{
			int m_Owner;
			vec2 m_Pos;
		};
		vec2 m_Pos{}, m_Vel{};
		std::array<CTee, 4> m_aTees{};
		std::array<CShell, 6> m_aShells{};
		int m_NumTees = 0, m_NumShells = 0;

		void ObserveTee(CTee Tee)
		{
			auto End = m_aTees.begin() + m_NumTees;
			const auto At = std::upper_bound(m_aTees.begin(), End, Tee.m_Distance, [](float Gap, const CTee &Other) { return Gap < Other.m_Distance; });
			if(At == m_aTees.end())
				return;
			if(m_NumTees < static_cast<int>(m_aTees.size()))
				++m_NumTees;
			std::move_backward(At, m_aTees.begin() + m_NumTees - 1, m_aTees.begin() + m_NumTees);
			*At = Tee;
		}
		void ObserveShell(CShell Shell)
		{
			if(m_NumShells < static_cast<int>(m_aShells.size()) && distance(m_Pos, Shell.m_Pos) <= 600.0f)
				m_aShells[m_NumShells++] = Shell;
		}
	};

	enum EAutopsyCause { CAUSE_UNKNOWN, CAUSE_BLAST, CAUSE_HAMMER, CAUSE_HOOK, CAUSE_CROWD, CAUSE_OWN };
	struct CAutopsyVerdict
	{
		EAutopsyCause m_Cause = CAUSE_UNKNOWN;
		int m_By = -1;
	};
	// 0x140287700 operates on the last 64 observations, newest first on ties.
	inline CAutopsyVerdict AutopsyVerdict(const std::deque<CAutopsyFrame> &Frames, int Own)
	{
		if(Frames.empty())
			return {};
		int Shock = -1;
		float Peak = 9.0f;
		for(int I = static_cast<int>(Frames.size()) - 1; I > 0; --I)
		{
			const float Change = distance(Frames[I].m_Vel, Frames[I - 1].m_Vel);
			if(Change > Peak)
			{
				Peak = Change;
				Shock = I;
			}
		}
		if(Shock >= 0)
		{
			for(int I = Shock; I >= std::max(0, Shock - 3); --I)
			{
				const auto &Frame = Frames[I];
				for(int J = 0; J < Frame.m_NumShells; ++J)
				{
					const auto &Shell = Frame.m_aShells[J];
					if(Shell.m_Owner != Own && distance(Frame.m_Pos, Shell.m_Pos) <= 135.0f)
						return {CAUSE_BLAST, Shell.m_Owner};
				}
			}
			const auto &Frame = Frames[Shock];
			for(int J = 0; J < Frame.m_NumTees; ++J)
			{
				const auto &Tee = Frame.m_aTees[J];
				if(Tee.m_Freeze < 1 && Tee.m_Distance <= 71.0f)
					return {CAUSE_HAMMER, Tee.m_Id};
			}
		}
		for(auto It = Frames.rbegin(); It != Frames.rend(); ++It)
			for(int J = 0; J < It->m_NumTees; ++J)
				if(It->m_aTees[J].m_Hooked == Own)
					return {CAUSE_HOOK, It->m_aTees[J].m_Id};

		const auto &Last = Frames.back();
		const int Crowd = std::count_if(Last.m_aTees.begin(), Last.m_aTees.begin() + Last.m_NumTees, [](const CAutopsyFrame::CTee &Tee) {
			return Tee.m_Freeze == 0 && Tee.m_Distance < 260.0f;
		});
		return {Crowd >= 2 ? CAUSE_CROWD : CAUSE_OWN, -1};
	}
}
#endif
