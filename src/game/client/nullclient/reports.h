#ifndef GAME_CLIENT_NULLCLIENT_REPORTS_H
#define GAME_CLIENT_NULLCLIENT_REPORTS_H
#include "controller.h"
#include "report_analysis.h"

#include <array>
#include <deque>
#include <string>
class CGameClient;
class IStorage;
namespace OpenKrx::NullClient
{
	struct CReportTiming
	{
		int m_TotalUs = 0, m_InitializeUs = 0, m_DecideUs = 0, m_PathUs = 0, m_JumpUs = 0;
		uint64_t m_Steps = 0, m_Copies = 0;
	};
	class CReports
	{
		struct CSlot
		{
			int m_Id = -1, m_LastTick = -1, m_LastReport = -1000, m_LastTrace = -1000, m_LastSlow = -1000;
			bool m_Alive = false, m_Frozen = false;
			std::deque<std::string> m_vTraceFrames;
			std::deque<std::string> m_vAutopsyFrames;
			std::deque<CAutopsyFrame> m_vEvidence;
			CReportTiming m_Timing;
			int m_TimingTick = -1;
			int m_Target = -1;
			std::array<bool, MAX_CLIENTS> m_aActive{};
			std::array<bool, MAX_CLIENTS> m_aFrozen{};
		};
		std::array<CSlot, 2> m_aSlots;
		std::string Frame(CGameClient *pClient, int Slot, const CController &Controller, const CNetObj_PlayerInput &Raw, CAutopsyFrame *pEvidence) const;

	public:
		void Reset() { m_aSlots = {}; }
		void Timing(int Slot, int Tick, CReportTiming Measurement)
		{
			m_aSlots[Slot].m_Timing = Measurement;
			m_aSlots[Slot].m_TimingTick = Tick;
		}
		void Update(CGameClient *pClient, const std::array<CController, 2> &Controllers, const std::array<CNetObj_PlayerInput, 2> &Raw, const std::array<int, 2> &Targets);
		bool Dump(CGameClient *pClient, int Slot, bool Autopsy, const char *pReason);
		void Kill(CGameClient *pClient, int Killer, int Victim);
		bool Clear(IStorage *pStorage, bool Autopsy);
	};
}
#endif
