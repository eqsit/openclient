#ifndef GAME_CLIENT_NULLCLIENT_COMPONENT_H
#define GAME_CLIENT_NULLCLIENT_COMPONENT_H

#include "controller.h"
#include "aimbot.h"
#include "helpers.h"
#include "fly.h"
#include "native_return.h"
#include "weapon_restore.h"

#include <game/client/component.h>

#include <array>
#include <string>

namespace OpenKrx::NullClient
{
	class CNullClient : public CComponent
	{
		void RenderVisuals();
		void RenderWeaponLines();
		CIdleTracker m_Idle;
		CHelpers m_Helpers;
		CFly m_Fly;
		CAimbot m_Aimbot;
		CNetObj_PlayerInput ApplyAvoid(CNetObj_PlayerInput Input, int Slot);
		std::array<CController, 2> m_aControllers;
		std::array<CWeaponRestore, 2> m_aWeaponRestore;
		std::array<CNativeReturnFeedback, 2> m_aNativeReturn;
		std::array<int, 2> m_aRestoreProbeTicks{-1, -1};
		std::array<bool, 2> m_aRestoreSafe{};
		std::array<CNetObj_PlayerInput, 2> m_aRestoreProbeInputs{};
		std::array<CNetObj_PlayerInput, 2> m_aRaw{};
		std::array<int, 2> m_aClientIds{-1, -1};
		int m_LastSlot = -1;
		bool m_WasSelected = false;

	public:
		int Sizeof() const override { return sizeof(*this); }
		void OnReset() override;
		void OnRender() override;
		void OnConsoleInit() override;
		bool OnInput(const IInput::CEvent &Event) override;
		bool OnCursorMove(float X, float Y, IInput::ECursorType CursorType) override;
		std::vector<std::string> Profiles();
		bool SaveProfile(const char *pName);
		bool LoadProfile(const char *pName);
		bool DeleteProfile(const char *pName);
		void OpenProfileFolder();
		CNetObj_PlayerInput Apply(CNetObj_PlayerInput Input, int Slot);
		bool Selected() const;
		bool ForceFakeAimSend(int Slot) const { return m_Helpers.ForceSend(Slot); }
		const CAimbot &Aimbot() const { return m_Aimbot; }
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
