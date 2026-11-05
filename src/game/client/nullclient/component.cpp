#include "component.h"

#include <base/mem.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>

namespace OpenKrx::NullClient
{
	bool CNullClient::Selected() const { return g_Config.m_OpenKrxAvoidProvider == 1; }

	void CNullClient::ConfigurePrediction(CGameWorld &World) const
	{
		// Original UpdatePrediction 1401f6410: snapshot ingestion already needs
		// these flags. Enabling them only in a later isolated copy loses shots
		// which NetObjAdd declined while normal AntiPing weapons was disabled.
		World.m_WorldConfig.m_NullPrediction = Selected();
		if(!Selected() || (!g_Config.m_NullGodmode && !g_Config.m_NullGodmodeDummy))
			return;
		if(World.m_WorldConfig.m_PredictFreeze == 0)
			World.m_WorldConfig.m_PredictFreeze = 1;
		if(g_Config.m_NullGodmodeWeapons)
			World.m_WorldConfig.m_PredictWeapons = true;
	}

	void CNullClient::OnReset()
	{
		m_Idle.Reset();
		m_Helpers.Reset();
		m_Fly.Reset();
		m_Aimbot.Reset();
		m_Serial.Reset();
		m_Reports.Reset();
		for(auto &Controller : m_aControllers)
			Controller.Reset();
		m_aRaw = {};
		m_aWeaponRestore = {};
		m_aNativeReturn = {};
		m_aRestoreProbeTicks = {-1, -1};
		m_aRestoreSafe = {};
		m_aRestoreProbeInputs = {};
		m_aClientIds = {-1, -1};
		m_LastSlot = -1;
		m_WasSelected = false;
	}

	CNetObj_PlayerInput CNullClient::ApplyAvoid(CNetObj_PlayerInput Input, int Slot)
	{
		if(!Selected() || Slot < 0 || Slot > 1 || GameClient()->m_Tas.Active())
			return Input;
		m_aRaw[Slot] = Input;
		auto Settings = SettingsFromConfig();
		// The UI uses OpenKRX's acknowledged grenade return in place of the
		// original immediate switch-back. Keep that policy out of the reference comparison.
		if(g_Config.m_NullRocketRestoreWeapon)
			Settings.m_RestoreWeapon = false;
		const bool Enabled = Slot == g_Config.m_ClDummy ? Settings.m_Enabled : Settings.m_Dummy;
		const int Id = GameClient()->m_aLocalIds[Slot];
		const auto *pCharacter = GameClient()->KrxPredictedWorld().GetCharacterById(Id);
		if(Client()->State() != IClient::STATE_ONLINE || GameClient()->m_Snap.m_SpecInfo.m_Active ||
			!(Input.m_PlayerFlags & PLAYERFLAG_PLAYING) || m_aClientIds[Slot] != Id || !pCharacter)
		{
			m_aWeaponRestore[Slot].Reset();
			m_aNativeReturn[Slot].Reset();
			m_aRestoreProbeTicks[Slot] = -1;
			return Input;
		}
		auto &Restore = m_aWeaponRestore[Slot];
		auto &State = m_aControllers[Slot].Output();
		const bool GrenadeAction = Enabled && State.m_HasPlan && State.m_WeaponAction == WEAPON_GRENADE;
		const int EmissionWeapon = State.m_WeaponAction >= 0 ? WeaponOnDirectInput(GameClient()->KrxPredictedWorld(), Id, Input) : pCharacter->GetActiveWeapon();
		const auto NativeInput = m_aNativeReturn[Slot].Prepare(Input, State, Settings);
		auto Output = State.Apply(NativeInput, Enabled, true, true, EmissionWeapon, Settings);
		m_aNativeReturn[Slot].Observe(Input, Output, Enabled && Settings.m_RestoreWeapon);
		const auto *pAuthoritative = GameClient()->KrxGameWorld().GetCharacterById(Id);
		if(!Enabled || !g_Config.m_NullRocketRestoreWeapon || !pAuthoritative ||
			(Restore.Weapon() >= 0 && !pAuthoritative->Core()->m_aWeapons[Restore.Weapon()].m_Got))
		{
			Restore.Reset();
			m_aRestoreProbeTicks[Slot] = -1;
			return Output;
		}
		const int Tick = Client()->PredGameTick(g_Config.m_ClDummy);
		if(GrenadeAction)
			m_aRestoreProbeTicks[Slot] = -1;
		if(!GrenadeAction && Restore.NeedsSafeExit() && (m_aRestoreProbeTicks[Slot] != Tick || mem_comp(&m_aRestoreProbeInputs[Slot], &Output, sizeof(Output)) != 0))
		{
			m_aRestoreProbeTicks[Slot] = Tick;
			m_aRestoreProbeInputs[Slot] = Output;
			m_aRestoreSafe[Slot] = false;
			CSimulation ProbeWorld;
			if(ProbeWorld.Initialize(GameClient()->KrxPredictedWorld(), Id, Settings, m_Idle.Idle()))
			{
				// Existing shells remain; a new grenade may not make this test
				// pass. This also covers an unnecessary switch without a shot.
				ProbeWorld.Character()->SetWeaponAmmo(WEAPON_GRENADE, 0);
				ProbeWorld.CommitRoot();
				const int Window = Settings.m_Horizon;
				const auto Result = Probe(ProbeWorld, Output, Window, true);
				m_aRestoreSafe[Slot] = Result.m_Life == Window;
			}
		}
		return Restore.Apply(Input, Output, GrenadeAction, EmissionWeapon,
			pAuthoritative->GetActiveWeapon(), pAuthoritative->GetAttackTick(),
			!GrenadeAction && m_aRestoreProbeTicks[Slot] == Tick && m_aRestoreSafe[Slot]);
	}

	CNetObj_PlayerInput CNullClient::Apply(CNetObj_PlayerInput Input, int Slot)
	{
		if(!Selected() || Slot < 0 || Slot > 1 || !(Input.m_PlayerFlags & PLAYERFLAG_PLAYING))
			return Input;
		auto Output = m_Fly.HasPlan(Slot) ? m_Fly.Apply(Input, Slot) : ApplyAvoid(m_Serial.Apply(GameClient(), Input, Slot), Slot);
		// Original main Controls 121a57..121ad0 derives these gates from
		// emitted changes, not whether the planner happens to hold a plan.
		const bool HookAim = Output.m_Hook != Input.m_Hook || Output.m_TargetX != Input.m_TargetX || Output.m_TargetY != Input.m_TargetY;
		const bool Changed = HookAim || Output.m_Direction != Input.m_Direction || Output.m_Jump != Input.m_Jump;
		const bool FireChanged = Output.m_Fire != Input.m_Fire;
		Output = m_Aimbot.Apply(GameClient(), Output, Slot, Changed);
		return m_Helpers.Apply(GameClient(), Output, Slot, Changed, FireChanged, HookAim);
	}
	void CNullClient::SynchronizeSlot()
	{
		if(!m_WasSelected || m_LastSlot != g_Config.m_ClDummy)
		{
			OnReset();
			m_LastSlot = g_Config.m_ClDummy;
			m_WasSelected = true;
		}
	}

	void CNullClient::OnRender()
	{
		if(!Selected() || Client()->State() != IClient::STATE_ONLINE)
		{
			if(m_WasSelected)
				OnReset();
			return;
		}
		SynchronizeSlot();
		if(GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->m_Tas.Active())
			return;
		m_Fly.Update(GameClient());
		m_Idle.Update(GameClient());
		m_Serial.Update(GameClient(), m_Warlist, m_Idle.Idle());
		m_Helpers.Update(GameClient());
		const auto Settings = SettingsFromConfig();
		for(int Slot = 0; Slot < 2; ++Slot)
		{
			const int Id = GameClient()->m_aLocalIds[Slot];
			if(m_aClientIds[Slot] != Id)
			{
				m_aControllers[Slot].Reset();
				m_aWeaponRestore[Slot].Reset();
				m_aNativeReturn[Slot].Reset();
				m_aRestoreProbeTicks[Slot] = -1;
				m_aClientIds[Slot] = Id;
			}
			if(Id < 0 || !GameClient()->m_Snap.m_aCharacters[Id].m_Active)
			{
				m_aControllers[Slot].Reset();
				m_aWeaponRestore[Slot].Reset();
				m_aNativeReturn[Slot].Reset();
				m_aRestoreProbeTicks[Slot] = -1;
				continue;
			}
			if(m_Fly.HasPlan(Slot) || !(Slot == g_Config.m_ClDummy ? Settings.m_Enabled : Settings.m_Dummy))
			{
				m_aControllers[Slot].Reset();
				m_aWeaponRestore[Slot].Reset();
				m_aNativeReturn[Slot].Reset();
				m_aRestoreProbeTicks[Slot] = -1;
				continue;
			}
			if(m_aControllers[Slot].LastTick() == Client()->PredGameTick(g_Config.m_ClDummy))
				continue;
			auto Raw = m_aRaw[Slot];
			if(!(Raw.m_PlayerFlags & PLAYERFLAG_PLAYING))
				Raw = GameClient()->m_Controls.m_aInputData[Slot];
			if(Slot == g_Config.m_ClDummy)
			{
				const auto &Controls = GameClient()->m_Controls;
				Raw.m_Direction = Controls.m_aInputDirectionLeft[Slot] == Controls.m_aInputDirectionRight[Slot] ? 0 : Controls.m_aInputDirectionLeft[Slot] ? -1 :
																					     1;
				Raw.m_Jump = Controls.m_PhysicalJump != 0;
				Raw.m_Hook = Controls.m_aPhysicalHook[Slot] != 0;
			}
			if(!(Raw.m_PlayerFlags & PLAYERFLAG_PLAYING))
				continue;
			Raw = m_Serial.Apply(GameClient(), Raw, Slot, false);
			const int64_t Start = time_get_impl();
			CSimulation World;
			if(!World.Initialize(GameClient()->KrxPredictedWorld(), Id, Settings, m_Idle.Idle()))
				continue;
			CFrame Frame;
			Frame.m_Tick = Client()->PredGameTick(g_Config.m_ClDummy);
			Frame.m_ResolvePendingWeapon = true;
			Frame.m_Dummy = Slot != g_Config.m_ClDummy;
			const auto &Snapshot = GameClient()->m_Snap.m_aCharacters[Id].m_Cur;
			Frame.m_AuthoritativeWeapon = Snapshot.m_Weapon;
			Frame.m_AuthoritativeAttackTick = Snapshot.m_AttackTick;
			m_aControllers[Slot].Decide(World, Settings, Raw, Frame);
			if(g_Config.m_NullPreserveJump && Slot == g_Config.m_ClDummy &&
				GameClient()->m_Controls.m_PhysicalJump && !GameClient()->m_Controls.m_ManualJumpConsumed)
				m_aControllers[Slot].PreserveManualJump(World, Settings, Raw, Frame);
			m_Reports.Timing(Slot, (time_get_impl() - Start) * 1000000 / time_freq());
		}
		m_Reports.Update(GameClient(), m_aControllers, m_aRaw);
		RenderVisuals();
	}
	void CNullClient::OnMessage(int MsgId, void *pRawMsg)
	{
		if(Selected() && MsgId == NETMSGTYPE_SV_KILLMSG)
		{
			const auto *p = static_cast<CNetMsg_Sv_KillMsg *>(pRawMsg);
			m_Reports.Kill(GameClient(), p->m_Killer, p->m_Victim);
		}
	}
}

namespace OpenKrx::NullClient
{
bool CNullClient::OnCursorMove(float X, float Y, IInput::ECursorType CursorType)
{
	if(!Selected() || GameClient()->m_Menus.IsActive() || GameClient()->m_Chat.IsActive() || GameClient()->m_GameConsole.IsActive()) return false;
	SynchronizeSlot();
	return m_Fly.OnCursorMove(GameClient(), X, Y);
}
}
