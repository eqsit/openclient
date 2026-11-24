#include "component.h"

#include <base/mem.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/map.h>
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
		InvalidateWorkerCollisions();
		m_vShotPaths.clear();
		m_ShotPathTick = -1;
		StopRescueScene();
		m_RescueSceneShown = false;
		m_vRescueSnapshots.clear();
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
		m_WasSelected = false;
	}

	CNetObj_PlayerInput CNullClient::ApplyAvoid(CNetObj_PlayerInput Input, int Slot)
	{
		if(!Selected() || Slot < 0 || Slot > 1 || GameClient()->m_Tas.Active())
			return Input;
		auto Settings = SettingsFromConfig();
		// The UI uses OpenKRX's acknowledged grenade return in place of the
		// original immediate switch-back. Keep that policy out of the reference comparison.
		if(g_Config.m_NullRocketRestoreWeapon)
			Settings.m_RestoreWeapon = false;
		const bool Dummy = Slot != g_Config.m_ClDummy;
		const bool Enabled = Dummy ? Settings.m_Dummy : Settings.m_Enabled;
		const int Id = GameClient()->m_aLocalIds[Slot];
		const auto *pCharacter = GameClient()->KrxPredictedWorld().GetCharacterById(Id);
		if(Client()->State() != IClient::STATE_ONLINE || GameClient()->m_Snap.m_SpecInfo.m_Active ||
			(!Dummy && !(Input.m_PlayerFlags & PLAYERFLAG_PLAYING)) || m_aClientIds[Slot] != Id || !pCharacter)
		{
			m_aWeaponRestore[Slot].Reset();
			m_aNativeReturn[Slot].Reset();
			m_aRestoreProbeTicks[Slot] = -1;
			// 0x140213c20:160-179 drops the restore target as soon as the local
			// character is gone; the controller itself survives the death.
			m_aControllers[Slot].Output().m_RestoreWeapon = -1;
			return Input;
		}
		auto &Restore = m_aWeaponRestore[Slot];
		auto &Controller = m_aControllers[Slot];
		const int Tick = Client()->PredGameTick(g_Config.m_ClDummy);
		Controller.ExpireOutput(Tick);
		if(!Enabled || !Controller.MatchesMode(Settings, Dummy) || (!Dummy && Settings.m_Mode == CSettings::MODE_LEGIT))
		{
			Restore.Reset();
			m_aNativeReturn[Slot].Reset();
			m_aRestoreProbeTicks[Slot] = -1;
			return Enabled && Controller.MatchesMode(Settings, Dummy) ? Controller.ApplyLegit(Input) : Input;
		}
		auto &State = Controller.Output();
		const bool GrenadeAction = State.m_HasPlan && State.m_WeaponAction == WEAPON_GRENADE;
		const int EmissionWeapon = State.m_WeaponAction >= 0 ? WeaponOnDirectInput(GameClient()->KrxPredictedWorld(), Id, Input) : pCharacter->GetActiveWeapon();
		const auto NativeInput = m_aNativeReturn[Slot].Prepare(Input, State, Settings);
		auto Output = State.Apply(NativeInput, true, true, true, EmissionWeapon, Settings);
		m_aNativeReturn[Slot].Observe(Input, Output, Settings.m_RestoreWeapon);
		const auto *pAuthoritative = GameClient()->KrxGameWorld().GetCharacterById(Id);
		if(!g_Config.m_NullRocketRestoreWeapon || !pAuthoritative ||
			(Restore.Weapon() >= 0 && !pAuthoritative->Core()->m_aWeapons[Restore.Weapon()].m_Got))
		{
			Restore.Reset();
			m_aRestoreProbeTicks[Slot] = -1;
			return Output;
		}
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

	CNetObj_PlayerInput CNullClient::SimulationInput(int Slot) const
	{
		const auto *pClient = GameClient();
		if(Slot < 0 || Slot >= NUM_DUMMIES)
			return {};
		const bool Dummy = Slot != g_Config.m_ClDummy;
		const int ClientId = pClient->m_aLocalIds[Slot];
		const bool Hammer = Dummy && ClientId >= 0 && ClientId < MAX_CLIENTS &&
			g_Config.m_ClDummyHammer && g_Config.m_NullGodmodeDummy;
		if(const auto *pPlan = m_Serial.Plan(Slot); pPlan && !Hammer)
			return *pPlan;

		const auto &Controls = pClient->m_Controls;
		auto Input = Controls.m_aInputData[Slot];
		if(Dummy)
		{
			Input.m_Fire = pClient->m_DummyInput.m_Fire;
			Input.m_WantedWeapon = pClient->m_DummyInput.m_WantedWeapon;
			if(Hammer)
			{
				const vec2 Aim = pClient->m_LocalCharacterPos - pClient->m_aClients[ClientId].m_Predicted.m_Pos;
				Input.m_TargetX = static_cast<int>(Aim.x);
				Input.m_TargetY = static_cast<int>(Aim.y);
				Input.m_WantedWeapon = WEAPON_HAMMER + 1;
				Input.m_Fire = (static_cast<unsigned>(Input.m_Fire) + 1u) & ~1u;
				Input.m_Fire |= pClient->m_DummyFire % 25 == 0;
			}
			if(g_Config.m_ClDummyCopyMoves || g_Config.m_ClDummyControl)
				return Input;
			if(g_Config.m_NullSerialKillerDummy)
			{
				Input.m_Direction = Input.m_Jump = Input.m_Hook = 0;
				return Input;
			}
		}
		Input.m_Direction = (Controls.m_aInputDirectionRight[Slot] != 0) - (Controls.m_aInputDirectionLeft[Slot] != 0);
		return Input;
	}

	bool CNullClient::DummyLaserReady(int Slot)
	{
		return Selected() && Client()->State() == IClient::STATE_ONLINE &&
			!GameClient()->m_Tas.Active() && !GameClient()->m_Tas.NullPlaybackActive() &&
			m_Helpers.DummyLaserReady(GameClient(), Slot);
	}

	CNetObj_PlayerInput CNullClient::Apply(CNetObj_PlayerInput Input, int Slot, bool SkipDummySerial, bool *pDummyLaserApplied)
	{
		if(!Selected() || Slot < 0 || Slot > 1 || GameClient()->m_Tas.Active())
			return Input;
		// 0x140213c20:98-138 masks the whole transform with an in-game/connection
		// check, so a disconnect between decision and emission sends raw input.
		if(Client()->State() != IClient::STATE_ONLINE)
			return Input;
		const bool Main = Slot == g_Config.m_ClDummy;
		m_aRaw[Slot] = Input;
		if(Main && !(Input.m_PlayerFlags & PLAYERFLAG_PLAYING))
			return Input;
		const auto SerialInput = !Main && SkipDummySerial ? Input : m_Serial.Apply(GameClient(), Input, Slot);
		auto Output = Main && m_Fly.Active() ? m_Fly.Apply(SerialInput, Slot) : ApplyAvoid(SerialInput, Slot);
		if(!Main)
		{
			// 0x1401f8800 compares the fields needed by dummy laser after Serial.
			if(Output.m_TargetX == SerialInput.m_TargetX && Output.m_TargetY == SerialInput.m_TargetY &&
				Output.m_Hook == SerialInput.m_Hook && Output.m_Fire == SerialInput.m_Fire &&
				Output.m_WantedWeapon == SerialInput.m_WantedWeapon && !GameClient()->m_Tas.NullPlaybackActive())
				{
				const bool Applied = m_Helpers.ApplyDummyLaser(GameClient(), Output, Slot);
				if(pDummyLaserApplied)
					*pDummyLaserApplied = Applied;
			}
			return Output;
		}
		// Original main Controls 121a57..121ad0 derives these gates from
		// emitted changes, not whether the planner happens to hold a plan.
		const bool AimChanged = Output.m_TargetX != Input.m_TargetX || Output.m_TargetY != Input.m_TargetY;
		const bool HookChanged = Output.m_Hook != Input.m_Hook;
		const bool DirectionChanged = Output.m_Direction != Input.m_Direction;
		const bool JumpChanged = Output.m_Jump != Input.m_Jump;
		const bool FireChanged = Output.m_Fire != SerialInput.m_Fire;
		// 0x140224d40:775 gates the helpers on Hook||TargetX||TargetY.
		const bool HookGate = HookChanged || AimChanged;
		const bool Changed = AimChanged || HookChanged || DirectionChanged || JumpChanged;
		Output = m_Aimbot.Apply(GameClient(), Output, Slot, Changed);
		return m_Helpers.Apply(GameClient(), Output, Slot, Changed, FireChanged, HookGate);
	}
	void CNullClient::SynchronizeSlot()
	{
		// Switching the dummy slot must not wipe the other slot: the original
		// keeps two independent module instances (client+0x537230/0x5436d8).
		if(!m_WasSelected)
		{
			OnReset();
			m_WasSelected = true;
		}
	}

	void CNullClient::OnRender()
	{
		if(Selected() && Client()->State() == IClient::STATE_DEMOPLAYBACK)
		{
			RenderRescueScene();
			return;
		}
		if(!Selected() || Client()->State() != IClient::STATE_ONLINE)
		{
			if(m_WasSelected || m_RescueSceneStart != 0 || m_RescueSceneShown)
				OnReset();
			return;
		}
		SynchronizeSlot();
		const int Tick = Client()->PredGameTick(g_Config.m_ClDummy);
		for(auto &Controller : m_aControllers)
			Controller.BeginTick(Tick);
		if(GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->m_Tas.Active())
			return;
		m_Idle.Update(GameClient());
		m_Serial.Update(GameClient(), m_Warlist, m_Idle.Idle());
		m_Fly.Update(GameClient());
		m_Helpers.Update(GameClient());
		if(!g_Config.m_NullGodmodeVisuals)
			m_vRescueSnapshots.clear();
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
				// 0x140213c20 keeps controller state and only drops the weapon
				// restore when the character disappears; the plan is cleared by
				// BeginTick, not by a full reset.
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
			const auto Raw = SimulationInput(Slot);
			if(Slot == g_Config.m_ClDummy && !(GameClient()->m_Controls.m_aInputData[Slot].m_PlayerFlags & PLAYERFLAG_PLAYING))
				continue;
			const int64_t Start = time_get_impl();
			CSimulation World;
			const bool Legit = Slot == g_Config.m_ClDummy && Settings.m_Mode == CSettings::MODE_LEGIT;
			if(!World.Initialize(GameClient()->KrxPredictedWorld(), Id, Settings, m_Idle.Idle(), Legit))
				continue;
			const int64_t Initialized = time_get_impl();
			CFrame Frame;
			Frame.m_Tick = Client()->PredGameTick(g_Config.m_ClDummy);
			Frame.m_ResolvePendingWeapon = true;
			Frame.m_Dummy = Slot != g_Config.m_ClDummy;
			Frame.m_DummyControlled = g_Config.m_ClDummyCopyMoves || g_Config.m_ClDummyControl || m_Serial.HasPlan(Slot);
			const auto &Snapshot = GameClient()->m_Snap.m_aCharacters[Id].m_Cur;
			Frame.m_AuthoritativeWeapon = Snapshot.m_Weapon;
			Frame.m_AuthoritativeAttackTick = Snapshot.m_AttackTick;
			CRescueSnapshot Rescue;
			if(g_Config.m_NullGodmodeVisuals && Slot == g_Config.m_ClDummy &&
				(m_vRescueSnapshots.empty() || Start - m_vRescueSnapshots.back().m_Time >= time_freq() * 18 / 100))
				Frame.m_pRescuePaths = &Rescue.m_vPaths;
			const int64_t DecideStart = time_get_impl();
			m_aControllers[Slot].Decide(World, Settings, Raw, Frame);
			const int64_t Decided = time_get_impl();
			int64_t PathTime = 0;
			if(Slot == g_Config.m_ClDummy && Settings.m_ShotPath)
			{
				m_vShotPaths = ProjectilePaths(World, Raw, Settings.m_ShotPathTicks);
				PathTime = time_get_impl() - Decided;
				m_ShotPathTick = Frame.m_Tick;
			}
			if(Frame.m_pRescuePaths)
			{
				Rescue.m_Time = time_get_impl();
				Rescue.m_Tick = Client()->GameTick(Slot);
				Rescue.m_MapCrc = GameClient()->Map()->Crc();
				if(!Rescue.m_vPaths.empty())
					m_SavedRescue = Rescue;
				m_vRescueSnapshots.push_back(std::move(Rescue));
				if(m_vRescueSnapshots.size() > 8)
					m_vRescueSnapshots.pop_front();
			}
			int64_t JumpTime = 0;
			if(g_Config.m_NullPreserveJump && Slot == g_Config.m_ClDummy &&
				GameClient()->m_Controls.m_PhysicalJump && !GameClient()->m_Controls.m_ManualJumpConsumed)
			{
				const int64_t JumpStart = time_get_impl();
				m_aControllers[Slot].PreserveManualJump(World, Settings, Raw, Frame);
				JumpTime = time_get_impl() - JumpStart;
			}
			const auto Us = [](int64_t Duration) { return static_cast<int>(Duration * 1000000 / time_freq()); };
			m_Reports.Timing(Slot, Client()->GameTick(Slot), {Us(time_get_impl() - Start), Us(Initialized - Start), Us(Decided - DecideStart), Us(PathTime), Us(JumpTime), World.TotalSteps(), World.TotalCopies()});
		}
		m_Reports.Update(GameClient(), m_aControllers, m_aRaw, {m_Serial.Target(0), m_Serial.Target(1)});
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
