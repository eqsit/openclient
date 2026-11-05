#ifndef GAME_CLIENT_NULLCLIENT_REPORTS_H
#define GAME_CLIENT_NULLCLIENT_REPORTS_H
#include "controller.h"

#include <array>
#include <deque>
#include <string>
class CGameClient;
class IStorage;
namespace OpenKrx::NullClient
{
	class CReports
	{
		struct CSlot
		{
			int m_Id = -1, m_LastTick = -1, m_LastReport = -1000, m_LastTrace = -1000, m_LastSlow = -1000;
			bool m_Alive = false, m_Frozen = false;
			std::deque<std::string> m_vFrames;
			int m_ControllerUs = 0;
			std::array<bool, MAX_CLIENTS> m_aFrozen{};
		};
		std::array<CSlot, 2> m_aSlots;
		std::string Frame(CGameClient *pClient, int Slot, const CController &Controller, const CNetObj_PlayerInput &Raw) const;

	public:
		void Reset() { m_aSlots = {}; }
		void Timing(int Slot, int Microseconds) { m_aSlots[Slot].m_ControllerUs = Microseconds; }
		void Update(CGameClient *pClient, const std::array<CController, 2> &Controllers, const std::array<CNetObj_PlayerInput, 2> &Raw);
		bool Dump(CGameClient *pClient, int Slot, bool Autopsy, const char *pReason);
		void Kill(CGameClient *pClient, int Killer, int Victim);
		void Clear(IStorage *pStorage, bool Autopsy);
	};
}
#endif
