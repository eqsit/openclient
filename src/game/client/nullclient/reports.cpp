#include "reports.h"

#include <base/io.h>

#include <engine/shared/client_profile.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/projectile_data.h>
#include <game/collision.h>

namespace OpenKrx::NullClient
{
	namespace
	{
		void Int(CJsonWriter &J, const char *pKey, int Value)
		{
			J.WriteAttribute(pKey);
			J.WriteIntValue(Value);
		}
		void Str(CJsonWriter &J, const char *pKey, const char *pValue)
		{
			J.WriteAttribute(pKey);
			J.WriteStrValue(pValue);
		}
		void Point(CJsonWriter &J, const char *pKey, vec2 V)
		{
			J.WriteAttribute(pKey);
			J.BeginArray();
			J.WriteIntValue(round_to_int(V.x * 1000));
			J.WriteIntValue(round_to_int(V.y * 1000));
			J.EndArray();
		}
		void Input(CJsonWriter &J, const char *pKey, const CNetObj_PlayerInput &I)
		{
			J.WriteAttribute(pKey);
			J.BeginObject();
			Int(J, "direction", I.m_Direction);
			Int(J, "jump", I.m_Jump);
			Int(J, "hook", I.m_Hook);
			Int(J, "fire", I.m_Fire);
			Int(J, "target_x", I.m_TargetX);
			Int(J, "target_y", I.m_TargetY);
			Int(J, "wanted_weapon", I.m_WantedWeapon);
			J.EndObject();
		}
	}
	std::string CReports::Frame(CGameClient *pClient, int Slot, const CController &Controller, const CNetObj_PlayerInput &Raw) const
	{
		CJsonStringWriter J;
		J.BeginObject();
		Int(J, "tick", pClient->Client()->GameTick(Slot));
		Int(J, "pred_tick", pClient->Client()->PredGameTick(Slot));
		Int(J, "slot", Slot);
		Int(J, "controller_us", m_aSlots[Slot].m_ControllerUs);
		Int(J, "own", pClient->m_aLocalIds[Slot]);
		Str(J, "stage", Controller.Stage());
		Int(J, "life", Controller.BaselineLife());
		Int(J, "react", Controller.Reaction());
		Input(J, "input", Raw);
		Input(J, "plan", Controller.Output().m_Plan);
		Int(J, "has_plan", Controller.Output().m_HasPlan);
		Int(J, "weapon_action", Controller.Output().m_WeaponAction);
		J.WriteAttribute("tees");
		J.BeginArray();
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			if(auto *p = pClient->KrxPredictedWorld().GetCharacterById(Id))
			{
				J.BeginObject();
				Int(J, "id", Id);
				Point(J, "pos", p->Core()->m_Pos);
				Point(J, "vel", p->Core()->m_Vel);
				Int(J, "freeze", p->m_FreezeTime);
				Int(J, "deep", p->Core()->m_DeepFrozen);
				Int(J, "weapon", p->GetActiveWeapon());
				Int(J, "hooked", p->Core()->HookedPlayer());
				Point(J, "hook_pos", p->Core()->m_HookPos);
				J.EndObject();
			}
		J.EndArray();
		J.WriteAttribute("projectiles");
		J.BeginArray();
		for(auto *p = pClient->KrxPredictedWorld().FindFirst(CGameWorld::ENTTYPE_PROJECTILE); p; p = p->TypeNext())
		{
			auto *pShot = static_cast<CProjectile *>(p);
			const auto Data = pShot->GetData();
			J.BeginObject();
			Int(J, "id", pShot->GetId());
			Int(J, "owner", pShot->GetOwner());
			Int(J, "weapon", Data.m_Type);
			Point(J, "start", Data.m_StartPos);
			Point(J, "direction", Data.m_StartVel);
			Int(J, "start_tick", pShot->GetStartTick());
			J.EndObject();
		}
		J.EndArray();
		J.EndObject();
		return J.GetOutputString();
	}
	bool CReports::Dump(CGameClient *pClient, int Slot, bool Autopsy, const char *pReason)
	{
		if(Slot < 0 || Slot > 1 || m_aSlots[Slot].m_vFrames.empty())
			return false;
		const bool Dummy = Slot != g_Config.m_ClDummy;
		const char *pPath = Autopsy ? (Dummy ? "null_autopsy_dummy.txt" : "null_autopsy.txt") : (Dummy ? "null_trace_dummy.txt" : "null_trace.txt");
		const std::string Temporary = std::string(pPath) + ".tmp";
		IOHANDLE File = pClient->Storage()->OpenFile(Temporary.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		CJsonStringWriter Header;
		Header.BeginObject();
		Str(Header, "format", "OpenClient Null report v1");
		Str(Header, "reason", pReason);
		Int(Header, "coordinate_scale", 1000);
		Int(Header, "slot", Slot);
		Header.WriteAttribute("settings");
		Header.BeginObject();
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) Int(Header, #ScriptName, g_Config.m_##Name);
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) Int(Header, #ScriptName, g_Config.m_##Name);
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc)
#include <engine/shared/nullclient_config.h>
#include <engine/shared/nullclient_features_config.h>
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
		Header.EndObject();
		Header.WriteAttribute("tuning_fixed100");
		Header.BeginArray();
		for(int Zone = 0; Zone < 256; ++Zone)
		{
			Header.BeginArray();
			const auto *pTune = pClient->KrxPredictedWorld().GetTuning(Zone);
			for(int i = 0; i < CTuningParams::Num(); ++i)
				Header.WriteIntValue(pTune->NetworkArray()[i]);
			Header.EndArray();
		}
		Header.EndArray();
		// Self-contained local tile snapshot; native report serialization is a host adaptation.
		const int Id = pClient->m_aLocalIds[Slot];
		if(Id >= 0 && Id < MAX_CLIENTS)
		{
			const vec2 Center = pClient->m_aClients[Id].m_RenderPos;
			const int X = (int)(Center.x / 32), Y = (int)(Center.y / 32);
			Int(Header, "tiles_x", X - 16);
			Int(Header, "tiles_y", Y - 16);
			Int(Header, "tiles_width", 33);
			Header.WriteAttribute("tiles");
			Header.BeginArray();
			for(int Dy = -16; Dy <= 16; ++Dy)
				for(int Dx = -16; Dx <= 16; ++Dx)
				{
					const int I = pClient->Collision()->GetPureMapIndex(vec2((X + Dx) * 32 + 16, (Y + Dy) * 32 + 16));
					Header.BeginArray();
					Header.WriteIntValue(pClient->Collision()->GetTileIndex(I));
					Header.WriteIntValue(pClient->Collision()->GetFrontTileIndex(I));
					Header.EndArray();
				}
			Header.EndArray();
		}
		Header.EndObject();
		std::string Text = "{\n\"header\":" + Header.GetOutputString() + ",\n\"frames\":[\n";
		bool First = true;
		for(const auto &Frame : m_aSlots[Slot].m_vFrames)
		{
			if(!First)
				Text += ",\n";
			Text += Frame;
			First = false;
		}
		Text += "\n]}\n";
		bool Ok = io_write(File, Text.data(), Text.size()) == Text.size();
		Ok &= io_close(File) == 0;
		if(Ok)
			Ok = pClient->Storage()->RenameFile(Temporary.c_str(), pPath, IStorage::TYPE_SAVE);
		if(!Ok)
			pClient->Storage()->RemoveFile(Temporary.c_str(), IStorage::TYPE_SAVE);
		return Ok;
	}
	void CReports::Update(CGameClient *pClient, const std::array<CController, 2> &Controllers, const std::array<CNetObj_PlayerInput, 2> &Raw)
	{
		if(!NullProfileActive() || pClient->Client()->State() != IClient::STATE_ONLINE)
			return;
		for(int Slot = 0; Slot < 2; ++Slot)
		{
			auto &S = m_aSlots[Slot];
			const int Id = pClient->m_aLocalIds[Slot];
			if(Id < 0 || Id >= MAX_CLIENTS)
			{
				S = {};
				continue;
			}
			const bool Trace = g_Config.m_NullGodmodeTrace && (Slot == g_Config.m_ClDummy ? g_Config.m_NullGodmode : g_Config.m_NullGodmodeDummy);
			if(!Trace && !g_Config.m_NullAutopsy)
			{
				S = {};
				continue;
			}
			if(S.m_Id != Id)
			{
				S = {};
				S.m_Id = Id;
			}
			const int Tick = pClient->Client()->GameTick(Slot);
			if(Tick == S.m_LastTick)
				continue;
			S.m_LastTick = Tick;
			const auto *p = pClient->KrxPredictedWorld().GetCharacterById(Id);
			const bool Alive = pClient->m_Snap.m_aCharacters[Id].m_Active && p;
			const bool Frozen = p && (p->m_FreezeTime > 0 || p->Core()->m_DeepFrozen);
			S.m_vFrames.push_back(Frame(pClient, Slot, Controllers[Slot], Raw[Slot]));
			while(S.m_vFrames.size() > 100)
				S.m_vFrames.pop_front();
			const bool Death = S.m_Alive && !Alive, Freeze = Alive && Frozen && !S.m_Frozen;
			if(Trace && (Death || Freeze) && Tick - S.m_LastTrace > 10)
			{
				S.m_LastTrace = Tick;
				Dump(pClient, Slot, false, Death ? "death" : "freeze");
			}
			if(Trace && S.m_ControllerUs >= 5000 && Tick - S.m_LastSlow > 250)
			{
				S.m_LastSlow = Tick;
				S.m_LastTrace = Tick;
				Dump(pClient, Slot, false, "slow tick");
			}
			if(g_Config.m_NullAutopsy && (Death || Freeze) && Tick - S.m_LastReport > 40 && S.m_vFrames.size() >= 8)
			{
				S.m_LastReport = Tick;
				Dump(pClient, Slot, true, Death ? "death" : "freeze");
			}
			if(g_Config.m_NullAutopsy > 1)
				for(int Other = 0; Other < MAX_CLIENTS; ++Other)
				{
					const auto *pOther = pClient->KrxPredictedWorld().GetCharacterById(Other);
					const bool OtherFrozen = pOther && (pOther->m_FreezeTime > 0 || pOther->Core()->m_DeepFrozen);
					if(Other != Id && OtherFrozen && !S.m_aFrozen[Other] && p && p->GetLastHammerHitClientId() == Other && Tick - S.m_LastReport > 40 && S.m_vFrames.size() >= 8)
					{
						S.m_LastReport = Tick;
						Dump(pClient, Slot, true, "target frozen");
					}
					S.m_aFrozen[Other] = OtherFrozen;
				}
			S.m_Alive = Alive;
			S.m_Frozen = Frozen;
		}
	}
	void CReports::Kill(CGameClient *pClient, int Killer, int Victim)
	{
		if(!NullProfileActive())
			return;
		for(int Slot = 0; Slot < 2; ++Slot)
		{
			const int Id = pClient->m_aLocalIds[Slot];
			if(Id < 0)
				continue;
			const int Tick = pClient->Client()->GameTick(Slot);
			if(Victim == Id && g_Config.m_NullGodmodeTrace && (Slot == g_Config.m_ClDummy ? g_Config.m_NullGodmode : g_Config.m_NullGodmodeDummy) && Tick - m_aSlots[Slot].m_LastTrace > 10)
			{
				m_aSlots[Slot].m_LastTrace = Tick;
				Dump(pClient, Slot, false, "death");
			}
			if(g_Config.m_NullAutopsy && (Victim == Id || (g_Config.m_NullAutopsy > 1 && Killer == Id && Victim != Id)) && Tick - m_aSlots[Slot].m_LastReport > 40)
			{
				m_aSlots[Slot].m_LastReport = Tick;
				Dump(pClient, Slot, true, Victim == Id ? "death" : "kill");
			}
		}
	}
	void CReports::Clear(IStorage *pStorage, bool Autopsy)
	{
		Reset();
		for(const char *p : Autopsy ? std::initializer_list<const char *>{"null_autopsy.txt", "null_autopsy_dummy.txt"} : std::initializer_list<const char *>{"null_trace.txt", "null_trace_dummy.txt"})
			pStorage->RemoveFile(p, IStorage::TYPE_SAVE);
	}
}
