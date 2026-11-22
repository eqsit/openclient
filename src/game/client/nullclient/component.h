#ifndef GAME_CLIENT_NULLCLIENT_COMPONENT_H
#define GAME_CLIENT_NULLCLIENT_COMPONENT_H

#include "controller.h"
#include "aimbot.h"
#include "helpers.h"
#include "fly.h"
#include "native_return.h"
#include "weapon_restore.h"
#include "serial.h"
#include "reports.h"
#include "projectile_paths.h"

#include <game/client/component.h>

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace OpenKrx::NullClient
{
	class CNullClient : public CComponent
	{
		void RenderVisuals();
		void RenderWeaponLines();
		struct CRescueSnapshot
		{
			std::vector<CRescuePath> m_vPaths;
			std::int64_t m_Time = 0;
			int m_Tick = -1;
			unsigned m_MapCrc = 0;
		};
		std::deque<CRescueSnapshot> m_vRescueSnapshots;
		std::optional<CRescueSnapshot> m_SavedRescue;
		std::int64_t m_RescueSceneStart = 0;
		bool m_RescueSceneWasPaused = false;
		bool m_RescueSceneShown = false;
		bool CaptureRescueScene();
		bool StartRescueScene(bool LogErrors);
		void StopRescueScene();
		void RenderRescueScene();
		void RenderRescuePaths();
		std::vector<std::string> m_vProfiles;
		std::int64_t m_NextProfileScan = 0;
		bool m_ProfilesDirty = true;
		CIdleTracker m_Idle;
		CHelpers m_Helpers;
		CFly m_Fly;
		CAimbot m_Aimbot;
		CSerial m_Serial;
		CWarlist m_Warlist;
		CReports m_Reports;
		CNetObj_PlayerInput ApplyAvoid(CNetObj_PlayerInput Input, int Slot);
		std::array<CController, 2> m_aControllers;
		std::vector<CProjectilePath> m_vShotPaths;
		int m_ShotPathTick = -1;
		std::array<CWeaponRestore, 2> m_aWeaponRestore;
		std::array<CNativeReturnFeedback, 2> m_aNativeReturn;
		std::array<int, 2> m_aRestoreProbeTicks{-1, -1};
		std::array<bool, 2> m_aRestoreSafe{};
		std::array<CNetObj_PlayerInput, 2> m_aRestoreProbeInputs{};
		std::array<CNetObj_PlayerInput, 2> m_aRaw{};
		std::array<int, 2> m_aClientIds{-1, -1};
		int m_LastSlot = -1;
		bool m_WasSelected = false;
		void SynchronizeSlot();

	public:
		int Sizeof() const override { return sizeof(*this); }
		void OnReset() override;
		void OnInit() override { m_Warlist.Load(Storage()); }
		void OnRender() override;
		void OnConsoleInit() override;
		void OnMessage(int MsgId, void *pRawMsg) override;
		bool OnInput(const IInput::CEvent &Event) override;
		bool OnCursorMove(float X, float Y, IInput::ECursorType CursorType) override;
		const std::vector<std::string> &Profiles(bool Refresh = false);
		bool SaveProfile(const char *pName);
		bool LoadProfile(const char *pName);
		bool DeleteProfile(const char *pName);
		void OpenProfileFolder();
		CNetObj_PlayerInput Apply(CNetObj_PlayerInput Input, int Slot);
		bool Selected() const;
		bool ForceFakeAimSend(int Slot) const { return m_Helpers.ForceSend(Slot); }
		const CAimbot &Aimbot() const { return m_Aimbot; }
		CWarlist &Warlist() { return m_Warlist; }
		bool SaveWarlist() { return m_Warlist.Save(Storage()); }
		bool HandleWarlistCommand(const char *pLine);
		bool VisibleAim(int Slot, vec2 &Aim) const { return m_Helpers.VisibleAim(Slot, Aim); }
		const CController &Controller(int Slot) const { return m_aControllers[Slot]; }
		void ConfigurePrediction(CGameWorld &World) const;
		void CancelWeaponReturn(int Slot)
		{
			if(!Selected() || Slot < 0 || Slot > 1)
				return;
			m_aControllers[Slot].Output().m_RestoreWeapon = -1;
			m_aNativeReturn[Slot].Reset();
			m_aWeaponRestore[Slot].Reset();
			m_aRestoreProbeTicks[Slot] = -1;
		}
	};
}

#endif
