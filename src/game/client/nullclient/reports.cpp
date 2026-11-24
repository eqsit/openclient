#include "reports.h"

#include <base/io.h>

#include <engine/shared/client_profile.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/serverbrowser.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/projectile_data.h>
#include <game/collision.h>
#include <game/version.h>

#include <cstring>
#include <iomanip>
#include <sstream>

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
			Int(J, "next_weapon", I.m_NextWeapon);
			Int(J, "prev_weapon", I.m_PrevWeapon);
			Int(J, "player_flags", I.m_PlayerFlags);
			J.EndObject();
		}
		int Number(const json_value &Object, const char *pKey)
		{
			return json_int_get(&Object[pKey]);
		}
		std::string Field(const json_value &Object, const char *pKey)
		{
			return Object[pKey].type == json_integer ? std::to_string(Number(Object, pKey)) : "none";
		}
		vec2 Coordinates(const json_value &Object, const char *pKey)
		{
			const auto &Pair = Object[pKey];
			return vec2(json_int_get(&Pair[0]), json_int_get(&Pair[1])) / 1000.0f;
		}
		std::string Keys(const json_value &Input, int PreviousFire)
		{
			if(Input.type != json_object)
				return "none";
			const int Direction = Number(Input, "direction");
			const char Fire = PreviousFire < 0 ? '?' : CountInput(PreviousFire, Number(Input, "fire")).m_Presses > 0 ? 'F' : '-';
			return {Direction < 0 ? 'L' : Direction > 0 ? 'R' : '-', Number(Input, "jump") ? 'J' : '-', Number(Input, "hook") ? 'H' : '-', Fire};
		}
		std::string Player(CGameClient *pClient, int Id)
		{
			if(Id < 0 || Id >= MAX_CLIENTS)
				return "none";
			std::string Name = pClient->m_aClients[Id].m_aName;
			for(char &C : Name)
				if(static_cast<unsigned char>(C) < 32 || C == '"')
					C = '?';
			return "id " + std::to_string(Id) + " \"" + Name + "\"";
		}
		bool WriteReadable(CGameClient *pClient, const char *pPath, const std::deque<std::string> &Frames, const std::deque<CAutopsyFrame> &Evidence, bool Autopsy, const char *pReason)
		{
			std::ostringstream Motion, Decisions, Physics, Trace, Header;
			int PreviousFire = -1, PreviousRawFire = -1;
			int Count = 0, PeakUs = -1, PeakTick = -1, PeakSteps = 0, PeakCopies = 0;
			int MaxSteps = 0, MaxCopies = 0;
			int64_t SumUs = 0, SumSteps = 0, SumCopies = 0;
			std::unique_ptr<json_value, decltype(&json_value_free)> Last(JsonParse(Frames.back().data(), Frames.back().size()), json_value_free);
			if(!Last)
				return false;
			const int EventTick = Number(*Last, "tick");
			const int Target = Number(*Last, "target");
			const int Id = Number(*Last, "own");
			for(const auto &Frame : Frames)
			{
				std::unique_ptr<json_value, decltype(&json_value_free)> Row(JsonParse(Frame.data(), Frame.size()), json_value_free);
				if(!Row)
					return false;
				const auto &Tees = (*Row)["tees"];
				const json_value *pSelf = nullptr, *pOther = nullptr;
				for(int I = 0; I < json_array_length(&Tees); ++I)
				{
					const auto &Tee = Tees[I];
					if(Number(Tee, "id") == Id) pSelf = &Tee;
					if(Number(Tee, "id") == Target) pOther = &Tee;
				}
				const auto &Sent = (*Row)["last_sent_input"];
				const auto &Raw = (*Row)["input"];
				const auto SentKeys = Keys(Sent, PreviousFire);
				const auto RawKeys = Keys(Raw, PreviousRawFire);
				PreviousFire = Sent.type == json_object ? Number(Sent, "fire") : -1;
				PreviousRawFire = Number(Raw, "fire");
				const int Tick = Number(*Row, "tick");
				const int Relative = Tick - EventTick;
				const char *pStage = json_string_get(&(*Row)["stage"]);
				if(Autopsy)
				{
					Motion << Relative;
					for(const auto *pTee : {pSelf, pOther})
					{
						if(!pTee)
						{
							Motion << " | none none | none none | -";
							continue;
						}
						const auto Pos = Coordinates(*pTee, "pos"), Vel = Coordinates(*pTee, "vel");
						const bool Frozen = Number(*pTee, "freeze") > 0 || Number(*pTee, "deep");
						Motion << " | " << Pos.x << ' ' << Pos.y << " | " << Vel.x << ' ' << Vel.y << " | " << (Frozen ? 'F' : Number(*pTee, "grounded") ? 'G' : 'A');
					}
					Motion << " | ";
					if(pSelf && pOther) Motion << distance(Coordinates(*pSelf, "pos"), Coordinates(*pOther, "pos"));
					else Motion << "none";
					Motion << '\n';
					Decisions << Relative << " | " << SentKeys << " | " << (pSelf ? Field(*pSelf, "weapon") : "none") << ' ' << (pSelf ? Field(*pSelf, "reload") : "none") << " | " << (pSelf ? Field(*pSelf, "hook_state") : "none") << " | " << pStage << '\n';
					Physics << Relative << " | ";
					if(pSelf)
					{
						const auto Pos = Coordinates(*pSelf, "pos"), Vel = Coordinates(*pSelf, "vel"), Hook = Coordinates(*pSelf, "hook_pos");
						Physics << std::fixed << std::setprecision(3) << Pos.x << ',' << Pos.y << " | " << Vel.x << ',' << Vel.y << " | " << Field(*pSelf, "jumped") << ' ' << Field(*pSelf, "freeze") << " | " << Hook.x << ',' << Hook.y;
					}
					else Physics << "none | none | none none | none";
					Physics << " | " << Field(Sent, "target_x") << ',' << Field(Sent, "target_y") << '\n';
				}
				else
				{
					Trace << Tick << " | " << pStage << " | " << Field(*Row, "life") << '/' << Field(*Row, "react") << " | " << RawKeys << " | " << SentKeys << " | " << Field(*Row, "moves") << " | ";
					if(pSelf) Trace << length(Coordinates(*pSelf, "vel"));
					else Trace << "none";
					Trace << " | " << (pSelf ? Field(*pSelf, "weapon") : "none") << " | ";
					if((*Row)["timing_tick"].type == json_integer)
					{
						const int Us = Number(*Row, "controller_us"), Steps = Number(*Row, "steps"), Copies = Number(*Row, "copies");
						++Count;
						SumUs += Us;
						SumSteps += Steps;
						SumCopies += Copies;
						MaxSteps = std::max(MaxSteps, Steps);
						MaxCopies = std::max(MaxCopies, Copies);
						if(Us > PeakUs)
						{
							PeakUs = Us;
							PeakTick = Number(*Row, "timing_tick");
							PeakSteps = Steps;
							PeakCopies = Copies;
						}
						Trace << Us / 1000.0;
					}
					else Trace << "none";
					Trace << " | " << Field(*Row, "steps") << " | " << Field(*Row, "prediction_ms") << " | " << Field(*Row, "ping_ms") << " | " << Field(*Row, "copies") << " | " << Field(*Row, "initialize_us") << ' ' << Field(*Row, "decide_us") << ' ' << Field(*Row, "projectile_us") << ' ' << Field(*Row, "manual_jump_us") << '\n';
				}
			}
			const auto &Config = pClient->KrxPredictedWorld().m_WorldConfig;
			if(Autopsy)
			{
				const bool Kill = !std::strcmp(pReason, "target frozen") || !std::strcmp(pReason, "kill");
				const char *pWhat = !std::strcmp(pReason, "freeze") ? "froze" : !std::strcmp(pReason, "deep-freeze") ? "deep-froze" : !std::strcmp(pReason, "death") ? "died" : "snapshot";
				const CAutopsyVerdict Verdict = Kill ? CAutopsyVerdict{} : AutopsyVerdict(Evidence, Id);
				const char *apCauses[] = {"UNKNOWN", "BLAST", "HAMMER", "HOOK-DRAG", "CROWD", "OWN-MOVEMENT"};
				Header << "# NULL AUTOPSY\nschema: opennull-autopsy/1\nbuild: " << (GIT_SHORTREV_HASH ? GIT_SHORTREV_HASH : "unknown")
				       << "\nconventions:\n  space: 32 world units per tile, x right, y down\n  speed: world units per tick, 50 ticks per second\n  t: relative to the last recorded tick\nevent:\n  kind: " << (Kill ? "KILL" : "DEATH") << "\n  what: " << pWhat << "\n  trigger: " << pReason << "\n  tick: " << EventTick << "\n  ticks_recorded: " << Frames.size()
				       << "\n  map: " << pClient->Client()->ServerInfo().m_aMap << "\n  server: " << pClient->Client()->ConnectAddressString()
				       << "\n  ping_ms: " << Field(*Last, "ping_ms") << "\n  prediction_ms: " << Field(*Last, "prediction_ms")
				       << "\n  predict_tiles: " << Config.m_PredictTiles << "   predict_freeze: " << Config.m_PredictFreeze << "\n  us: " << Player(pClient, Id) << "\n  opponent: " << Player(pClient, Target)
				       << "\ncause:\n  verdict: " << (Kill ? "not applicable - this is a report of a kill" : apCauses[Verdict.m_Cause])
				       << "\n  by: " << (Verdict.m_By >= 0 ? Player(pClient, Verdict.m_By) : "none identified");
				Header << "\nmotion: world units, velocity per tick\nt | us_x us_y | us_vx us_vy | st | them_x them_y | them_vx them_vy | st | gap\n" << Motion.str()
				       << "decisions:\nt | sent keys | weapon reload | hook state | god\n" << Decisions.str()
				       << "physics_detail:\nt | position(x,y) | velocity(x,y) | jumped freeze | hook_anchor(x,y) | sent_aim(x,y)\n" << Physics.str();
			}
			else
			{
				Header << "==== " << pReason << " at tick " << EventTick << ", last " << Frames.size() << " ticks ====\nserver " << pClient->Client()->ConnectAddressString()
				       << "  predict-tiles " << Config.m_PredictTiles << "  predict-freeze " << Config.m_PredictFreeze
				       << "\ncost: ";
				if(Count)
					Header << std::fixed << std::setprecision(3) << "peak " << PeakUs / 1000.0 << " ms at tick " << PeakTick << " (" << PeakSteps << " steps, " << PeakCopies
					       << " copies), mean " << SumUs / (1000.0 * Count) << " ms, " << static_cast<double>(SumSteps) / Count << " steps and " << static_cast<double>(SumCopies) / Count
					       << " copies per measured tick, peak steps " << MaxSteps << ", peak copies " << MaxCopies << ", measured " << Count;
				else
					Header << "none";
				Header << "\ntick | state | life/react | you | sent | moves | spd | weapon | ms | steps | pred | ping | simulation copies | phases_us init decide projectiles manual_jump\n" << Trace.str();
			}

			IStorage *pStorage = pClient->Storage();
			if(auto Previous = pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_SAVE))
			{
				char First;
				const bool Legacy = io_read(Previous, &First, 1) == 1 && First == '{';
				io_close(Previous);
				if(Legacy)
				{
					const std::string Base = std::string(pPath) + ".v1";
					std::string Backup = Base + ".json";
					for(int Index = 1; pStorage->FileExists(Backup.c_str(), IStorage::TYPE_SAVE); ++Index)
						Backup = Base + "." + std::to_string(Index) + ".json";
					if(!pStorage->RenameFile(pPath, Backup.c_str(), IStorage::TYPE_SAVE))
						return false;
				}
			}
			const std::string Text = Header.str() + "\n";
			IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_APPEND, IStorage::TYPE_SAVE);
			if(!File)
				return false;
			bool Ok = io_write(File, Text.data(), Text.size()) == Text.size();
			Ok &= io_close(File) == 0;
			return Ok;
		}
	}
	std::string CReports::Frame(CGameClient *pClient, int Slot, const CController &Controller, const CNetObj_PlayerInput &Raw, CAutopsyFrame *pEvidence) const
	{
		CJsonStringWriter J;
		J.BeginObject();
		Int(J, "tick", pClient->Client()->GameTick(Slot));
		Int(J, "pred_tick", pClient->Client()->PredGameTick(Slot));
		Int(J, "slot", Slot);
		const auto &SlotState = m_aSlots[Slot];
		const int Age = pClient->Client()->GameTick(Slot) - SlotState.m_TimingTick;
		const bool Measured = SlotState.m_TimingTick >= 0 && Age >= 0 && Age <= 1;
		Int(J, "controller_us", Measured ? SlotState.m_Timing.m_TotalUs : 0);
		if(Measured)
		{
			Int(J, "timing_tick", SlotState.m_TimingTick);
			J.WriteAttribute("steps");
			J.WriteIntValue(SlotState.m_Timing.m_Steps);
			J.WriteAttribute("copies");
			J.WriteIntValue(SlotState.m_Timing.m_Copies);
			Int(J, "initialize_us", SlotState.m_Timing.m_InitializeUs);
			Int(J, "decide_us", SlotState.m_Timing.m_DecideUs);
			Int(J, "projectile_us", SlotState.m_Timing.m_PathUs);
			Int(J, "manual_jump_us", SlotState.m_Timing.m_JumpUs);
		}
		Int(J, "target", SlotState.m_Target);
		Int(J, "moves", Controller.Planner().Candidates().size());
		Int(J, "prediction_ms", pClient->Client()->GetPredictionTime());
		if(const auto *pInfo = pClient->m_Snap.m_apPlayerInfos[pClient->m_aLocalIds[Slot]])
			Int(J, "ping_ms", pInfo->m_Latency);
		Int(J, "own", pClient->m_aLocalIds[Slot]);
		Str(J, "stage", Controller.Stage());
		Int(J, "life", Controller.BaselineLife());
		Int(J, "react", Controller.Reaction());
		Input(J, "input", Raw);
		Input(J, "plan", Controller.Output().m_Plan);
		const int ActiveSlot = g_Config.m_ClDummy;
		if(const int *pData = pClient->Client()->GetInput(pClient->Client()->PredGameTick(ActiveSlot), Slot != ActiveSlot))
		{
			CNetObj_PlayerInput Sent;
			std::memcpy(&Sent, pData, sizeof(Sent));
			Input(J, "last_sent_input", Sent);
		}
		Int(J, "has_plan", Controller.Output().m_HasPlan);
		Int(J, "weapon_action", Controller.Output().m_WeaponAction);
		auto &World = pClient->KrxPredictedWorld();
		const int Own = pClient->m_aLocalIds[Slot];
		if(pEvidence)
		{
			const auto *pSelf = World.GetCharacterById(Own);
			pEvidence->m_Pos = pSelf ? pSelf->GetPos() : pClient->m_aClients[Own].m_RenderPos;
			if(pSelf)
				pEvidence->m_Vel = pSelf->Core()->m_Vel;
		}
		J.WriteAttribute("tees");
		J.BeginArray();
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		{
			auto *p = World.GetCharacterById(Id);
			if(pEvidence && Id != Own && pClient->m_Snap.m_aCharacters[Id].m_Active)
			{
				const vec2 Pos = p ? p->GetPos() : pClient->m_aClients[Id].m_RenderPos;
				pEvidence->ObserveTee({Id, p ? p->m_FreezeTime : 0, p ? p->Core()->HookedPlayer() : -1, distance(Pos, pEvidence->m_Pos)});
			}
			if(p)
			{
				J.BeginObject();
				Int(J, "id", Id);
				Str(J, "name", pClient->m_aClients[Id].m_aName);
				Point(J, "pos", p->Core()->m_Pos);
				Point(J, "vel", p->Core()->m_Vel);
				Int(J, "freeze", p->m_FreezeTime);
				Int(J, "deep", p->Core()->m_DeepFrozen);
				Int(J, "live", p->Core()->m_LiveFrozen);
				Int(J, "weapon", p->GetActiveWeapon());
				Int(J, "grounded", p->IsGrounded());
				Int(J, "reload", p->DiagnosticReloadTimer());
				Int(J, "jumped", p->Core()->m_Jumped);
				Int(J, "hook_state", p->Core()->m_HookState);
				Int(J, "hooked", p->Core()->HookedPlayer());
				Point(J, "hook_pos", p->Core()->m_HookPos);
				J.EndObject();
			}
		}
		J.EndArray();
		J.WriteAttribute("projectiles");
		J.BeginArray();
		for(auto *p = pClient->KrxPredictedWorld().FindFirst(CGameWorld::ENTTYPE_PROJECTILE); p; p = p->TypeNext())
		{
			auto *pShot = static_cast<CProjectile *>(p);
			const auto Data = pShot->GetData();
			const vec2 Current = pShot->GetPos((World.GameTick() - pShot->GetStartTick()) / 50.0f);
			if(pEvidence)
				pEvidence->ObserveShell({pShot->GetOwner(), Current});
			J.BeginObject();
			Int(J, "id", pShot->GetId());
			Int(J, "owner", pShot->GetOwner());
			Int(J, "weapon", Data.m_Type);
			Point(J, "pos", Current);
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
		if(Slot < 0 || Slot > 1)
			return false;
		const auto &Frames = Autopsy ? m_aSlots[Slot].m_vAutopsyFrames : m_aSlots[Slot].m_vTraceFrames;
		if(Frames.empty())
			return false;
		const bool Dummy = Slot != g_Config.m_ClDummy;
		const char *pPath = Autopsy ? (Dummy ? "null_autopsy_dummy.txt" : "null_autopsy.txt") : (Dummy ? "null_trace_dummy.txt" : "null_trace.txt");
		const std::string JsonPath = std::string(pPath, std::strlen(pPath) - 4) + ".json";
		const std::string Temporary = JsonPath + ".tmp";
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
		for(const auto &Frame : Frames)
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
			Ok = pClient->Storage()->RenameFile(Temporary.c_str(), JsonPath.c_str(), IStorage::TYPE_SAVE);
		if(!Ok)
			pClient->Storage()->RemoveFile(Temporary.c_str(), IStorage::TYPE_SAVE);
		return Ok && WriteReadable(pClient, pPath, Frames, m_aSlots[Slot].m_vEvidence, Autopsy, pReason);
	}
	void CReports::Update(CGameClient *pClient, const std::array<CController, 2> &Controllers, const std::array<CNetObj_PlayerInput, 2> &Raw, const std::array<int, 2> &Targets)
	{
		if(!NullProfileActive() || pClient->Client()->State() != IClient::STATE_ONLINE)
		{
			// Do not let a measurement taken on one of these frames look fresh later.
			for(auto &S : m_aSlots)
				S.m_TimingTick = -1;
			return;
		}
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
			const bool Autopsy = g_Config.m_NullAutopsy != 0;
			if(!Trace && !Autopsy)
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
			S.m_Target = Targets[Slot];
			const auto *p = pClient->KrxPredictedWorld().GetCharacterById(Id);
			const bool Alive = pClient->m_Snap.m_aCharacters[Id].m_Active && p;
			const bool Frozen = p && (p->m_FreezeTime > 0 || p->Core()->m_DeepFrozen);
			CAutopsyFrame Evidence;
			auto Contents = Frame(pClient, Slot, Controllers[Slot], Raw[Slot], Autopsy ? &Evidence : nullptr);
			if(Trace)
			{
				S.m_vTraceFrames.push_back(Autopsy ? Contents : std::move(Contents));
				if(S.m_vTraceFrames.size() > 100)
					S.m_vTraceFrames.pop_front();
			}
			if(Autopsy)
			{
				S.m_vAutopsyFrames.push_back(std::move(Contents));
				S.m_vEvidence.push_back(Evidence);
				if(S.m_vAutopsyFrames.size() > 64)
				{
					S.m_vAutopsyFrames.pop_front();
					S.m_vEvidence.pop_front();
				}
			}
			const bool Death = S.m_Alive && !Alive, Freeze = Alive && Frozen && !S.m_Frozen;
			if(Trace && (Death || Freeze) && Tick - S.m_LastTrace > 10)
			{
				S.m_LastTrace = Tick;
				Dump(pClient, Slot, false, Death ? "death" : "freeze");
			}
			if(Trace && S.m_TimingTick >= 0 && Tick >= S.m_TimingTick && Tick - S.m_TimingTick <= 1 && S.m_Timing.m_TotalUs >= 5000 && Tick - S.m_LastSlow > 250)
			{
				S.m_LastSlow = Tick;
				S.m_LastTrace = Tick;
				Dump(pClient, Slot, false, "slow tick");
			}
			if(Autopsy && (Death || Freeze) && Tick - S.m_LastReport > 40 && S.m_vAutopsyFrames.size() >= 8)
			{
				S.m_LastReport = Tick;
				Dump(pClient, Slot, true, Death ? "death" : p->Core()->m_DeepFrozen ? "deep-freeze" : "freeze");
			}
			if(g_Config.m_NullAutopsy > 1)
				for(int Other = 0; Other < MAX_CLIENTS; ++Other)
				{
					const auto *pOther = pClient->KrxPredictedWorld().GetCharacterById(Other);
					const bool Active = Other != Id && pClient->m_Snap.m_aCharacters[Other].m_Active;
					const bool OtherFrozen = pOther && (pOther->m_FreezeTime > 0 || pOther->Core()->m_DeepFrozen);
					if(Active && S.m_aActive[Other] && OtherFrozen && !S.m_aFrozen[Other] && S.m_Target == Other && Tick - S.m_LastReport > 40 && S.m_vAutopsyFrames.size() >= 8)
					{
						S.m_LastReport = Tick;
						Dump(pClient, Slot, true, "target frozen");
					}
					S.m_aFrozen[Other] = OtherFrozen;
					S.m_aActive[Other] = Active;
				}
			S.m_Alive = Alive;
			S.m_Frozen = Frozen;
			S.m_TimingTick = -1;
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
	bool CReports::Clear(IStorage *pStorage, bool Autopsy)
	{
		for(auto &Slot : m_aSlots)
		{
			if(Autopsy)
			{
				Slot.m_vAutopsyFrames.clear();
				Slot.m_vEvidence.clear();
				Slot.m_LastReport = -1000;
				Slot.m_aFrozen = {};
				Slot.m_aActive = {};
			}
			else
			{
				Slot.m_vTraceFrames.clear();
				Slot.m_LastTrace = -1000;
				Slot.m_LastSlow = -1000;
			}
		}
		bool Removed = false;
		for(const char *p : Autopsy ? std::initializer_list<const char *>{"null_autopsy.txt", "null_autopsy_dummy.txt", "null_autopsy.json", "null_autopsy_dummy.json"} : std::initializer_list<const char *>{"null_trace.txt", "null_trace_dummy.txt", "null_trace.json", "null_trace_dummy.json"})
			Removed |= pStorage->RemoveFile(p, IStorage::TYPE_SAVE);
		return Removed;
	}
}
