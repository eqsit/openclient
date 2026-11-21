#include "krx_tas.h"

#include <base/bytes.h>
#include <base/io.h>
#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/map.h>
#include <engine/shared/client_profile.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/nullclient/projectile_paths.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <string_view>

namespace OpenKrx
{
	namespace
	{
		constexpr size_t MAX_NULL_TAS_FRAMES = 5000000;
		constexpr int64_t NULL_TAS_FRAME_BYTES = 12 * sizeof(uint32_t);
		constexpr size_t NULL_TAS_CHECKPOINT_INTERVAL = 64;
		constexpr int64_t NANOSECONDS = 1000000000;
		constexpr char NULL_TAS_MAGIC[] = "NTAS";
		constexpr int NULL_TAS_FORMAT_VERSION = 3;

		constexpr auto aInputMembers = {
			&CNetObj_PlayerInput::m_Direction,
			&CNetObj_PlayerInput::m_TargetX,
			&CNetObj_PlayerInput::m_TargetY,
			&CNetObj_PlayerInput::m_Jump,
			&CNetObj_PlayerInput::m_Fire,
			&CNetObj_PlayerInput::m_Hook,
			&CNetObj_PlayerInput::m_PlayerFlags,
			&CNetObj_PlayerInput::m_WantedWeapon,
			&CNetObj_PlayerInput::m_NextWeapon,
			&CNetObj_PlayerInput::m_PrevWeapon,
		};

		std::string NullTasPath(const char *pName)
		{
			if(!pName)
				return {};
			std::string_view Name(pName);
			if(Name.ends_with(".tas"))
				Name.remove_suffix(4);
			if(Name.empty() || Name.size() >= 64 || Name.front() == '.' || Name.find_first_of("/\\:") != std::string_view::npos || Name.find("..") != std::string_view::npos)
				return {};
			return "tas/" + std::string(Name) + ".tas";
		}

		bool ReadNullInt(IOHANDLE File, int &Value)
		{
			unsigned char aBytes[4];
			if(io_read(File, aBytes, sizeof(aBytes)) != sizeof(aBytes))
				return false;
			Value = std::bit_cast<int32_t>(static_cast<uint32_t>(bytes_be_to_uint(aBytes)));
			return true;
		}

		bool WriteNullInt(IOHANDLE File, int Value)
		{
			unsigned char aBytes[4];
			uint_to_bytes_be(aBytes, static_cast<uint32_t>(Value));
			return io_write(File, aBytes, sizeof(aBytes)) == sizeof(aBytes);
		}

		struct CNullTasFile
		{
			std::vector<CReplayFrame> m_vFrames;
			std::string m_Map;
			vec2 m_Start = vec2(0, 0);
			bool m_AllWeapons = false;
		};

		bool ReadNullTas(IOHANDLE File, CNullTasFile &Tas)
		{
			const int64_t Length = io_length(File);
			char aMagic[4];
			int Version, X, Y, Count;
			if(io_read(File, aMagic, sizeof(aMagic)) != sizeof(aMagic) || std::memcmp(aMagic, NULL_TAS_MAGIC, sizeof(aMagic)) != 0 ||
				!ReadNullInt(File, Version) || Version < 1 || Version > NULL_TAS_FORMAT_VERSION ||
				!ReadNullInt(File, X) || !ReadNullInt(File, Y))
				return false;
			Tas.m_Start = vec2(X, Y);
			int64_t HeaderBytes = 5 * sizeof(uint32_t);
			if(Version >= 2)
			{
				int MapLength, AllWeapons;
				if(!ReadNullInt(File, MapLength) || MapLength < 0 || MapLength >= MAX_MAP_LENGTH)
					return false;
				Tas.m_Map.resize(MapLength);
				if(io_read(File, Tas.m_Map.data(), MapLength) != static_cast<unsigned>(MapLength) || Tas.m_Map.find('\0') != std::string::npos ||
					!ReadNullInt(File, AllWeapons))
					return false;
				Tas.m_AllWeapons = AllWeapons != 0;
				HeaderBytes += 2 * sizeof(uint32_t) + MapLength;
			}
			if(Version >= 3)
			{
				int StartTick, FinishTick;
				if(!ReadNullInt(File, StartTick) || !ReadNullInt(File, FinishTick))
					return false;
				HeaderBytes += 2 * sizeof(uint32_t);
			}
			if(!ReadNullInt(File, Count) || Count < 0 || static_cast<size_t>(Count) > MAX_NULL_TAS_FRAMES ||
				Length < HeaderBytes + static_cast<int64_t>(Count) * NULL_TAS_FRAME_BYTES)
				return false;
			Tas.m_vFrames.reserve(Count);
			for(int Index = 0; Index < Count; ++Index)
			{
				CReplayFrame Frame{};
				for(auto Member : aInputMembers)
					if(!ReadNullInt(File, Frame.m_Input.*Member))
						return false;
				if(!ReadNullInt(File, X) || !ReadNullInt(File, Y))
					return false;
				Frame.m_Position = vec2(X, Y);
				Tas.m_vFrames.push_back(Frame);
			}
			return true;
		}

		bool TouchesNullTasTile(const CCollision &Collision, vec2 Pos, int Type)
		{
			const int Index = Collision.GetMapIndex(Pos);
			return Collision.GetTileIndex(Index) == Type || Collision.GetFrontTileIndex(Index) == Type;
		}

		bool WriteNullTas(IOHANDLE File, const std::vector<CReplayFrame> &vFrames, std::string_view Map, vec2 Start, bool AllWeapons, const CCollision &Collision)
		{
			int StartTick = -1;
			int FinishTick = -1;
			for(size_t Tick = 0; Tick < vFrames.size(); ++Tick)
			{
				const vec2 Pos = vFrames[Tick].m_Position;
				if(StartTick < 0 && TouchesNullTasTile(Collision, Pos, TILE_START))
					StartTick = static_cast<int>(Tick);
				if(TouchesNullTasTile(Collision, Pos, TILE_FINISH))
				{
					FinishTick = static_cast<int>(Tick);
					break;
				}
			}
			if(io_write(File, NULL_TAS_MAGIC, 4) != 4 || !WriteNullInt(File, NULL_TAS_FORMAT_VERSION) ||
				!WriteNullInt(File, round_to_int(Start.x)) || !WriteNullInt(File, round_to_int(Start.y)) ||
				!WriteNullInt(File, static_cast<int>(Map.size())) || io_write(File, Map.data(), Map.size()) != Map.size() ||
				!WriteNullInt(File, AllWeapons) || !WriteNullInt(File, StartTick) || !WriteNullInt(File, FinishTick) ||
				!WriteNullInt(File, static_cast<int>(vFrames.size())))
				return false;
			for(const auto &Frame : vFrames)
			{
				for(auto Member : aInputMembers)
					if(!WriteNullInt(File, Frame.m_Input.*Member))
						return false;
				if(!WriteNullInt(File, round_to_int(Frame.m_Position.x)) || !WriteNullInt(File, round_to_int(Frame.m_Position.y)))
					return false;
			}
			return true;
		}

		bool KeyMatches(int Key, int Binding)
		{
			return Binding > KEY_UNKNOWN && Binding < KEY_LAST && Key == Binding;
		}

		bool KeyHeld(const IInput &Input, int Binding)
		{
			return Binding > KEY_UNKNOWN && Binding < KEY_LAST && Input.KeyIsPressed(Binding);
		}

		CNetObj_PlayerInput ReleasedInput(const CNetObj_PlayerInput &Raw, const CNetObj_PlayerInput &Last)
		{
			auto Packet = Raw;
			Packet.m_Direction = Packet.m_Jump = Packet.m_Hook = Packet.m_WantedWeapon = 0;
			Packet.m_Fire = (Last.m_Fire + (Last.m_Fire & 1)) & INPUT_STATE_MASK;
			Packet.m_NextWeapon = Last.m_NextWeapon;
			Packet.m_PrevWeapon = Last.m_PrevWeapon;
			return Packet;
		}

		bool NullTasFrozen(const CCharacter &Character)
		{
			return Character.m_FreezeTime > 0 || Character.Core()->m_DeepFrozen || Character.Core()->m_LiveFrozen;
		}
	}

	bool CKrxTas::NullTasAvailable() const
	{
		return NullProfileActive() && g_Config.m_NullTasEnabled && Client()->State() == IClient::STATE_ONLINE;
	}

	bool CKrxTas::NullEditorActive() const
	{
		return NullTasAvailable() && m_NullTas.m_Editor && m_NullTas.m_pWorld &&
			m_NullTas.m_Slot == g_Config.m_ClDummy && m_NullTas.m_ClientId == GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	}

	bool CKrxTas::NullPlaybackActive() const
	{
		return NullTasAvailable() && m_NullTas.m_Playback && m_NullTas.m_Slot == g_Config.m_ClDummy;
	}

	void CKrxTas::NullTasMessage(const char *pMessage)
	{
		m_NullTas.m_Status = pMessage;
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Null TAS", pMessage);
	}

	void CKrxTas::ResetNullTasWorlds()
	{
		auto &Tas = m_NullTas;
		Tas.m_vCheckpoints.clear();
		Tas.m_pPreviousWorld.reset();
		Tas.m_pWorld.reset();
		Tas.m_pInitialWorld.reset();
		Tas.m_pCollision.reset();
		Tas.m_Tick = 0;
	}

	void CKrxTas::ResetNullTas()
	{
		LeaveNullTasProfile();
		ResetNullTasWorlds();
		m_NullTas.m_vFrames.clear();
		m_NullTas.m_Map.clear();
		m_NullTas.m_Name.clear();
		m_NullTas.m_Status.clear();
		m_NullTas.m_Dirty = false;
	}

	void CKrxTas::LeaveNullTasProfile()
	{
		auto &Tas = m_NullTas;
		Tas.m_Editor = Tas.m_Playback = Tas.m_PlaybackAligned = Tas.m_Boost = false;
		Tas.m_Seek = Tas.m_Coast = Tas.m_FireState = 0;
		Tas.m_StepTime = 0;
		Tas.m_LastPlaybackTick = -1;
		Tas.m_Flowing = Tas.m_BoostUseful = false;
		Tas.m_LastBoost = Tas.m_LastPreview = 0;
	}

	bool CKrxTas::StartNullEditor(bool NewRun)
	{
		if(!NullTasAvailable())
			return false;
		auto &Tas = m_NullTas;
		const int Slot = g_Config.m_ClDummy;
		const int Id = GameClient()->m_aLocalIds[Slot];
		CGameWorld &Source = GameClient()->m_GameWorld;
		if(GameClient()->m_Snap.m_SpecInfo.m_Active || !Source.GetCharacterById(Id))
		{
			NullTasMessage("An active tee is required to start the editor.");
			return false;
		}
		const char *pMap = GameClient()->Map()->FullName();
		if(!NewRun && !Tas.m_Map.empty() && Tas.m_Map != pMap)
		{
			NullTasMessage("This run belongs to a different map.");
			return false;
		}
		if(!NewRun && Tas.m_vFrames.empty() && !Tas.m_pWorld)
		{
			NullTasMessage("Start a new run or load a saved run.");
			return false;
		}
		if(NewRun || !Tas.m_pWorld || Id != Tas.m_ClientId || Slot != Tas.m_Slot)
		{
			const size_t Tick = NewRun ? 0 : Tas.m_Tick;
			ResetNullTasWorlds();
			Tas.m_pCollision = Collision()->CreateIsolatedCopy();
			std::copy_n(Source.TuningList(), Tas.m_aTuning.size(), Tas.m_aTuning.begin());
			Tas.m_pWorld = std::make_unique<CGameWorld>();
			Tas.m_pWorld->CopyWorldIsolated(&Source);
			Tas.m_pWorld->m_WorldConfig.m_PredictTiles = true;
			Tas.m_pWorld->m_WorldConfig.m_PredictFreeze = 1;
			Tas.m_pWorld->m_WorldConfig.m_PredictWeapons = true;
			Tas.m_pWorld->m_WorldConfig.m_PredictDDRace = true;
			Tas.m_pWorld->m_WorldConfig.m_KrxPrediction = false;
			Tas.m_pWorld->m_WorldConfig.m_NullPrediction = true;
			Tas.m_pWorld->m_Core.m_IsNullPrediction = true;
			Tas.m_pWorld->RebindCollision(Tas.m_pCollision.get());
			Tas.m_pWorld->RebindTuning(Tas.m_aTuning.data());
			Tas.m_pWorld->m_PredictedEvents.clear();
			for(int Player = 0; Player < MAX_CLIENTS; ++Player)
				if(Player != Id)
					if(auto *pCharacter = Tas.m_pWorld->GetCharacterById(Player))
						Tas.m_pWorld->RemoveCharacter(pCharacter);
			auto *pCharacter = Tas.m_pWorld->GetCharacterById(Id);
			if(NewRun)
			{
				Tas.m_vFrames.clear();
				Tas.m_Start = pCharacter->Core()->m_Pos;
				Tas.m_Map = pMap;
				Tas.m_Name.clear();
				Tas.m_Dirty = false;
				Tas.m_Autosaved = false;
				Tas.m_AllWeapons = g_Config.m_NullTasAllWeapons != 0;
			}
			else
			{
				auto Core = pCharacter->GetCore();
				Core.m_Pos = Core.m_HookPos = Tas.m_Start;
				Core.m_Vel = vec2(0, 0);
				Core.SetHookedPlayer(-1);
				Core.m_HookState = HOOK_IDLE;
				pCharacter->SetCore(Core);
				pCharacter->m_Pos = pCharacter->m_PrevPos = Tas.m_Start;
			}
			for(int Weapon = WEAPON_HAMMER; Weapon <= WEAPON_LASER; ++Weapon)
			{
				Tas.m_aInitialWeaponGot[Weapon] = pCharacter->GetWeaponGot(Weapon);
				Tas.m_aInitialWeaponAmmo[Weapon] = pCharacter->GetWeaponAmmo(Weapon);
				if(Tas.m_AllWeapons)
					pCharacter->GiveWeapon(Weapon);
			}
			Tas.m_EditorInput = pCharacter->DiagnosticLatestInput();
			Tas.m_Weapon = pCharacter->GetActiveWeapon();
			Tas.m_pInitialWorld = std::make_unique<CGameWorld>();
			Tas.m_pInitialWorld->CopyWorldIsolated(Tas.m_pWorld.get());
			Tas.m_pPreviousWorld = std::make_unique<CGameWorld>();
			Tas.m_pPreviousWorld->CopyWorldIsolated(Tas.m_pWorld.get());
			Tas.m_ClientId = Id;
			Tas.m_Slot = Slot;
			SeekNullTas(Tick);
		}
		Tas.m_LastStep = Tas.m_LastAutosave = time_get_nanoseconds().count();
		Tas.m_Editor = true;
		Tas.m_Playback = Tas.m_PlaybackAligned = false;
		Tas.m_Seek = Tas.m_Coast = Tas.m_FireState = 0;
		Tas.m_StepTime = 0;
		Tas.m_LastBoost = Tas.m_LastPreview = 0;
		NullTasMessage("Editor active.");
		return true;
	}

	void CKrxTas::StopNullEditor()
	{
		if(!m_NullTas.m_Editor)
			return;
		if(m_NullTas.m_Dirty && !m_NullTas.m_Autosaved)
			WriteNullTas("autosave", true);
		LeaveNullTasProfile();
		NullTasMessage("Editor closed.");
	}

	void CKrxTas::StepNullTasWorld(CGameWorld &World, int ClientId, const CNetObj_PlayerInput &Input)
	{
		if(auto *pCharacter = World.GetCharacterById(ClientId))
		{
			pCharacter->OnDirectInput(&Input);
			++World.m_GameTick;
			pCharacter->OnPredictedInput(&Input);
			World.Tick();
			World.m_PredictedEvents.clear();
		}
	}

	void CKrxTas::SeekNullTas(size_t Tick)
	{
		auto &Tas = m_NullTas;
		Tick = std::min(Tick, Tas.m_vFrames.size());
		if(Tick == Tas.m_Tick)
			return;
		const size_t Checkpoint = std::min(Tick / NULL_TAS_CHECKPOINT_INTERVAL, Tas.m_vCheckpoints.size());
		Tas.m_pWorld->CopyWorldIsolated(Checkpoint == 0 ? Tas.m_pInitialWorld.get() : Tas.m_vCheckpoints[Checkpoint - 1].get());
		for(size_t Index = Checkpoint * NULL_TAS_CHECKPOINT_INTERVAL; Index < Tick; ++Index)
		{
			StepNullTasWorld(*Tas.m_pWorld, Tas.m_ClientId, Tas.m_vFrames[Index].m_Input);
			if((Index + 1) % NULL_TAS_CHECKPOINT_INTERVAL == 0)
			{
				auto pCheckpoint = std::make_unique<CGameWorld>();
				pCheckpoint->CopyWorldIsolated(Tas.m_pWorld.get());
				Tas.m_vCheckpoints.push_back(std::move(pCheckpoint));
			}
		}
		Tas.m_Tick = Tick;
		Tas.m_pPreviousWorld->CopyWorldIsolated(Tas.m_pWorld.get());
		if(auto *pCharacter = Tas.m_pWorld->GetCharacterById(Tas.m_ClientId))
		{
			Tas.m_EditorInput = ReleasedInput(pCharacter->DiagnosticLatestInput(), pCharacter->DiagnosticLatestInput());
			Tas.m_Weapon = pCharacter->GetActiveWeapon();
		}
		Tas.m_FireState = 0;
		Tas.m_LastPreview = 0;
	}

	void CKrxTas::SetNullTasWeapon(int Weapon)
	{
		auto &Tas = m_NullTas;
		if(auto *pCharacter = Tas.m_pWorld->GetCharacterById(Tas.m_ClientId))
			if(Weapon >= WEAPON_HAMMER && Weapon <= WEAPON_LASER && pCharacter->GetWeaponGot(Weapon))
			{
				pCharacter->SetWeapon(Weapon);
				Tas.m_Weapon = Weapon;
				Tas.m_LastPreview = 0;
				Tas.m_LastBoost = 0;
			}
	}

	void CKrxTas::UpdateNullTasBoost(vec2 Aim)
	{
		auto &Tas = m_NullTas;
		const auto Now = time_get_nanoseconds().count();
		const auto Elapsed = Now - Tas.m_LastBoost;
		if(Tas.m_LastBoost && Elapsed < NANOSECONDS / 2 &&
			(Elapsed < NANOSECONDS / 20 || distance(Aim, Tas.m_BoostReference) <= 12.0f))
			return;
		Tas.m_LastBoost = Now;
		Tas.m_BoostReference = Aim;
		if(length(Aim) <= .001f)
			Aim = vec2(1, 0);
		const vec2 UnitAim = normalize(Aim);
		constexpr int Ticks = 42;
		constexpr int Angles = 32;
		std::array<vec2, Ticks> aBaseline{};
		CGameWorld Probe;
		float Best = 6.0f;
		Tas.m_BoostUseful = false;
		for(int Move : {-1, 0, 1})
		{
			auto Input = ReleasedInput(Tas.m_EditorInput, Tas.m_EditorInput);
			Input.m_Direction = Move;
			Input.m_WantedWeapon = Tas.m_Weapon + 1;
			const int Released = Input.m_Fire;
			Probe.CopyWorldIsolated(Tas.m_pWorld.get());
			for(int Tick = 0; Tick < Ticks; ++Tick)
			{
				StepNullTasWorld(Probe, Tas.m_ClientId, Input);
				auto *pCharacter = Probe.GetCharacterById(Tas.m_ClientId);
				aBaseline[Tick] = pCharacter ? pCharacter->Core()->m_Pos : vec2(0, 0);
			}
			for(int Angle = 0; Angle < Angles; ++Angle)
			{
				Probe.CopyWorldIsolated(Tas.m_pWorld.get());
				const vec2 Direction = direction(2.0f * pi * Angle / Angles);
				Input.m_TargetX = round_to_int(Direction.x * 100.0f);
				Input.m_TargetY = round_to_int(Direction.y * 100.0f);
				for(int Tick = 0; Tick < Ticks; ++Tick)
				{
					Input.m_Fire = (Released + (Tick == 0 ? 1 : 2)) & INPUT_STATE_MASK;
					StepNullTasWorld(Probe, Tas.m_ClientId, Input);
					auto *pCharacter = Probe.GetCharacterById(Tas.m_ClientId);
					if(!pCharacter)
						break;
					const float Gain = dot(pCharacter->Core()->m_Pos - aBaseline[Tick], UnitAim);
					if(Gain > Best)
					{
						Best = Gain;
						Tas.m_BoostAim = Direction;
						Tas.m_BoostMove = Move;
						Tas.m_BoostUseful = true;
					}
				}
			}
		}
	}

	void CKrxTas::UpdateNullEditor(CNetObj_PlayerInput Raw)
	{
		auto &Tas = m_NullTas;
		const auto Now = time_get_nanoseconds().count();
		const int64_t Elapsed = std::clamp<int64_t>(Now - Tas.m_LastStep, 0, NANOSECONDS);
		Tas.m_LastStep = Now;
		if(GameClient()->m_Menus.IsActive() || GameClient()->m_Chat.IsActive() || GameClient()->m_GameConsole.IsActive() ||
			!(Raw.m_PlayerFlags & PLAYERFLAG_PLAYING))
		{
			Tas.m_StepTime = 0;
			Tas.m_Flowing = false;
			return;
		}
		if(Tas.m_AllWeapons != (g_Config.m_NullTasAllWeapons != 0))
		{
			Tas.m_AllWeapons = g_Config.m_NullTasAllWeapons != 0;
			auto *pCharacter = Tas.m_pInitialWorld->GetCharacterById(Tas.m_ClientId);
			if(pCharacter)
			{
				for(int Weapon = WEAPON_HAMMER; Weapon <= WEAPON_LASER; ++Weapon)
				{
					pCharacter->SetWeaponGot(Weapon, Tas.m_AllWeapons || Tas.m_aInitialWeaponGot[Weapon]);
					pCharacter->SetWeaponAmmo(Weapon, Tas.m_AllWeapons ? -1 : Tas.m_aInitialWeaponAmmo[Weapon]);
				}
				if(pCharacter->GetActiveWeapon() < 0 || !pCharacter->GetWeaponGot(pCharacter->GetActiveWeapon()))
					pCharacter->SetWeapon(WEAPON_GUN);
			}
			Tas.m_vCheckpoints.clear();
			const size_t Tick = Tas.m_Tick;
			Tas.m_Tick = static_cast<size_t>(-1);
			SeekNullTas(Tick);
			Tas.m_Autosaved = false;
			Tas.m_Dirty = !Tas.m_vFrames.empty();
		}
		if(Tas.m_Seek != 0)
		{
			const auto Target = std::max<int64_t>(0, static_cast<int64_t>(Tas.m_Tick) + Tas.m_Seek);
			SeekNullTas(static_cast<size_t>(Target));
			Tas.m_Seek = Tas.m_Coast = 0;
			Tas.m_StepTime = 0;
			Tas.m_Flowing = false;
			return;
		}
		Raw.m_WantedWeapon = Tas.m_Weapon + 1;
		Raw.m_NextWeapon = Raw.m_PrevWeapon = 0;
		Raw.m_Fire = Tas.m_EditorInput.m_Fire;
		Tas.m_EditorInput = Raw;
		if(Tas.m_Boost)
			UpdateNullTasBoost(vec2(Raw.m_TargetX, Raw.m_TargetY));
		const bool Driving = Raw.m_Direction || Raw.m_Jump || Raw.m_Hook || Tas.m_FireState ||
			KeyHeld(*Input(), g_Config.m_NullTasKeyFlow);
		if(Driving)
			Tas.m_Coast = g_Config.m_NullTasCoast;
		Tas.m_Flowing = Driving || Tas.m_Coast > 0;
		if(!Tas.m_Flowing)
		{
			Tas.m_StepTime = 0;
			return;
		}
		const int64_t Interval = NANOSECONDS / std::clamp(g_Config.m_NullTasTps, 1, 50);
		Tas.m_StepTime += Elapsed;
		while(Tas.m_StepTime >= Interval && Tas.m_Tick < MAX_NULL_TAS_FRAMES && (Driving || Tas.m_Coast > 0))
		{
			auto *pCharacter = Tas.m_pWorld->GetCharacterById(Tas.m_ClientId);
			if(!pCharacter)
			{
				Tas.m_Flowing = false;
				Tas.m_StepTime = 0;
				break;
			}
			auto StepInput = Tas.m_EditorInput;
			if(!Driving)
			{
				StepInput.m_Direction = StepInput.m_Jump = StepInput.m_Hook = 0;
				--Tas.m_Coast;
			}
			if(Tas.m_FireState == 2)
			{
				StepInput.m_Fire = (StepInput.m_Fire + (StepInput.m_Fire & 1)) & INPUT_STATE_MASK;
				Tas.m_FireState = 0;
			}
			else if(Tas.m_FireState == 1 && pCharacter->DiagnosticReloadTimer() == 0)
			{
				const int Released = StepInput.m_Fire + (StepInput.m_Fire & 1);
				StepInput.m_Fire = (Released + 1) & INPUT_STATE_MASK;
				Tas.m_FireState = 2;
				if(Tas.m_Boost && Tas.m_BoostUseful)
				{
					const vec2 Aim = Tas.m_BoostAim * std::clamp(length(vec2(Raw.m_TargetX, Raw.m_TargetY)), 80.0f, 400.0f);
					StepInput.m_TargetX = round_to_int(Aim.x);
					StepInput.m_TargetY = round_to_int(Aim.y);
				}
			}
			const vec2 PreviousPos = pCharacter->Core()->m_Pos;
			Tas.m_vCheckpoints.resize(Tas.m_Tick / NULL_TAS_CHECKPOINT_INTERVAL);
			Tas.m_vFrames.resize(Tas.m_Tick);
			Tas.m_vFrames.push_back({StepInput, PreviousPos});
			Tas.m_EditorInput.m_Fire = StepInput.m_Fire;
			Tas.m_pPreviousWorld->CopyWorldIsolated(Tas.m_pWorld.get());
			StepNullTasWorld(*Tas.m_pWorld, Tas.m_ClientId, StepInput);
			++Tas.m_Tick;
			Tas.m_StepTime -= Interval;
			Tas.m_Dirty = true;
			Tas.m_Autosaved = false;
			if(Tas.m_Tick % NULL_TAS_CHECKPOINT_INTERVAL == 0)
			{
				auto pCheckpoint = std::make_unique<CGameWorld>();
				pCheckpoint->CopyWorldIsolated(Tas.m_pWorld.get());
				Tas.m_vCheckpoints.push_back(std::move(pCheckpoint));
			}
			pCharacter = Tas.m_pWorld->GetCharacterById(Tas.m_ClientId);
			if(g_Config.m_NullTasFreezeRewind && pCharacter && NullTasFrozen(*pCharacter))
			{
				const size_t Rewind = g_Config.m_NullTasFreezeRewindTicks;
				SeekNullTas(Tas.m_Tick - std::min(Tas.m_Tick, Rewind));
				Tas.m_Coast = 0;
				Tas.m_StepTime = 0;
				Tas.m_Flowing = false;
				break;
			}
		}
		if(!Driving && Tas.m_Coast == 0)
		{
			Tas.m_Flowing = false;
			Tas.m_StepTime = 0;
		}
	}

	bool CKrxTas::OnInput(const IInput::CEvent &Event)
	{
		if(!NullTasAvailable())
			return false;
		const int Key = Event.m_Key;
		const bool Pressed = (Event.m_Flags & (IInput::FLAG_PRESS | IInput::FLAG_REPEAT)) == IInput::FLAG_PRESS;
		if(!NullEditorActive())
		{
			if(Pressed && !GameClient()->m_Menus.IsActive() && KeyMatches(Key, g_Config.m_NullBindTas))
			{
				ToggleNullPlayback();
				return true;
			}
			return false;
		}
		if(KeyMatches(Key, g_Config.m_NullTasKeyMenu))
		{
			if(Pressed)
				GameClient()->m_Menus.SetActive(!GameClient()->m_Menus.IsActive());
			return true;
		}
		if(GameClient()->m_Menus.IsActive())
			return false;
		auto &Tas = m_NullTas;
		if(Key == KEY_MOUSE_1)
		{
			if(Pressed && Tas.m_FireState == 0 && Tas.m_Weapon != WEAPON_NINJA)
				Tas.m_FireState = 1;
			return true;
		}
		if(Key >= KEY_1 && Key <= KEY_5)
		{
			if(Pressed)
				SetNullTasWeapon(Key - KEY_1);
			return true;
		}
		if(KeyMatches(Key, g_Config.m_NullTasKeySeekBack) || Key == KEY_MOUSE_WHEEL_DOWN ||
			KeyMatches(Key, g_Config.m_NullTasKeySeekFwd) || Key == KEY_MOUSE_WHEEL_UP)
		{
			if(Pressed || (Event.m_Flags & IInput::FLAG_REPEAT))
				Tas.m_Seek += (KeyMatches(Key, g_Config.m_NullTasKeySeekBack) || Key == KEY_MOUSE_WHEEL_DOWN) ? -1 : 1;
			return true;
		}
		if(KeyMatches(Key, g_Config.m_NullTasKeySpeedUp) || KeyMatches(Key, g_Config.m_NullTasKeySpeedDown))
		{
			if(Pressed)
				g_Config.m_NullTasTps = std::clamp(g_Config.m_NullTasTps + (KeyMatches(Key, g_Config.m_NullTasKeySpeedUp) ? 2 : -2), 1, 50);
			return true;
		}
		if(KeyMatches(Key, g_Config.m_NullTasKeyBoost))
		{
			if(Pressed)
			{
				Tas.m_Boost = !Tas.m_Boost;
				Tas.m_LastBoost = 0;
			}
			return true;
		}
		return KeyMatches(Key, g_Config.m_NullTasKeyFlow) || KeyMatches(Key, g_Config.m_NullTasKeyLaser);
	}

	void CKrxTas::ToggleNullPlayback()
	{
		if(!NullTasAvailable() || NullEditorActive())
			return;
		auto &Tas = m_NullTas;
		if(Tas.m_Playback)
		{
			Tas.m_Playback = false;
			NullTasMessage("Playback stopped.");
			return;
		}
		if(Tas.m_vFrames.empty())
		{
			NullTasMessage("Load a run before playing it.");
			return;
		}
		if(!Tas.m_Map.empty() && Tas.m_Map != GameClient()->Map()->FullName())
		{
			NullTasMessage("This run belongs to a different map.");
			return;
		}
		Tas.m_Slot = g_Config.m_ClDummy;
		Tas.m_Playback = true;
		Tas.m_PlaybackAligned = false;
		Tas.m_PlaybackTick = 0;
		Tas.m_LastPlaybackTick = -1;
		NullTasMessage("Playback armed. Move to the start marker.");
	}

	void CKrxTas::UpdateNullPlayback(const CNetObj_PlayerInput &Raw, const CNetObj_PlayerInput &Last, CNetObj_PlayerInput &Packet)
	{
		auto &Tas = m_NullTas;
		const int Tick = Client()->PredGameTick(g_Config.m_ClDummy);
		if(Tas.m_LastPlaybackTick == Tick)
		{
			Packet = Tas.m_PlaybackInput;
			Packet.m_PlayerFlags = Raw.m_PlayerFlags;
			return;
		}
		Tas.m_LastPlaybackTick = Tick;
		if(!Tas.m_PlaybackAligned)
		{
			Packet = Raw;
			const auto *pCharacter = GameClient()->m_GameWorld.GetCharacterById(GameClient()->m_aLocalIds[g_Config.m_ClDummy]);
			if(pCharacter && std::abs(Tas.m_Start.y - pCharacter->Core()->m_Pos.y) <= 40.0f)
			{
				const auto *pCore = pCharacter->Core();
				const float DeltaX = Tas.m_Start.x - pCore->m_Pos.x;
				Packet.m_Direction = std::abs(DeltaX) > 0.5f ? (DeltaX > 0.0f ? 1 : -1) :
					std::abs(pCore->m_Vel.x) > 0.15f ? (pCore->m_Vel.x > 0.0f ? -1 : 1) : 0;
				Tas.m_PlaybackAligned = std::abs(DeltaX) <= 0.5f && length(pCore->m_Vel) <= 0.15f;
			}
			Tas.m_PlaybackInput = Packet;
			return;
		}
		if(Tas.m_PlaybackTick >= Tas.m_vFrames.size())
		{
			if(!g_Config.m_NullTasAutoReplay)
			{
				Tas.m_Playback = false;
				Packet = ReleasedInput(Raw, Last);
				NullTasMessage("Playback finished.");
				return;
			}
			Tas.m_PlaybackTick = 0;
		}
		const auto &Input = Tas.m_vFrames[Tas.m_PlaybackTick].m_Input;
		Packet = Input;
		Packet.m_PlayerFlags = Raw.m_PlayerFlags;
		Packet.m_Fire = Last.m_Fire;
		Packet.m_NextWeapon = Last.m_NextWeapon;
		Packet.m_PrevWeapon = Last.m_PrevWeapon;
		if(Tas.m_PlaybackTick > 0)
		{
			const auto &Previous = Tas.m_vFrames[Tas.m_PlaybackTick - 1].m_Input;
			for(auto Member : {&CNetObj_PlayerInput::m_Fire, &CNetObj_PlayerInput::m_NextWeapon, &CNetObj_PlayerInput::m_PrevWeapon})
				Packet.*Member = (static_cast<unsigned>(Last.*Member) + static_cast<unsigned>(Input.*Member) - static_cast<unsigned>(Previous.*Member)) & INPUT_STATE_MASK;
		}
		++Tas.m_PlaybackTick;
		Tas.m_PlaybackInput = Packet;
	}

	bool CKrxTas::ProcessNullTasInput(const CNetObj_PlayerInput &Raw, const CNetObj_PlayerInput &Last, CNetObj_PlayerInput &Packet)
	{
		if(m_NullTas.m_Editor && !NullEditorActive())
			LeaveNullTasProfile();
		if(m_NullTas.m_Playback && !NullPlaybackActive())
			LeaveNullTasProfile();
		if(NullEditorActive())
		{
			Packet = ReleasedInput(Raw, Last);
			UpdateNullEditor(Raw);
			return true;
		}
		if(NullPlaybackActive())
		{
			if(!(Raw.m_PlayerFlags & PLAYERFLAG_PLAYING) || GameClient()->m_Snap.m_SpecInfo.m_Active)
				return false;
			UpdateNullPlayback(Raw, Last, Packet);
			return true;
		}
		return false;
	}

	void CKrxTas::UpdateNullTasPreview()
	{
		auto &Tas = m_NullTas;
		const auto Now = time_get_nanoseconds().count();
		if(Tas.m_LastPreview && Now - Tas.m_LastPreview < NANOSECONDS / 25)
			return;
		Tas.m_LastPreview = Now;
		for(auto &vLine : Tas.m_avPreview)
			vLine.clear();
		const auto *pSource = Tas.m_pWorld->GetCharacterById(Tas.m_ClientId);
		if(!pSource)
			return;
		const int Ticks = std::clamp(g_Config.m_NullTasPreviewLen, 10, 150);
		CGameWorld Probe;
		for(int Kind = 0; Kind < 3; ++Kind)
		{
			if(Kind == 2 ? !g_Config.m_NullTasHookPreview :
					   !g_Config.m_NullTasFirePreview || (Kind == 1 && (Tas.m_Weapon <= WEAPON_HAMMER || Tas.m_Weapon >= WEAPON_NINJA)))
				continue;
			auto &vPath = Tas.m_avPreview[Kind];
			Probe.CopyWorldIsolated(Tas.m_pWorld.get());
			auto Input = Tas.m_EditorInput;
			Input.m_Fire = (Input.m_Fire + (Input.m_Fire & 1)) & INPUT_STATE_MASK;
			const int Released = Input.m_Fire;
			if(Kind == 1 && Tas.m_Boost && Tas.m_BoostUseful)
			{
				const vec2 Aim = Tas.m_BoostAim * std::clamp(length(vec2(Input.m_TargetX, Input.m_TargetY)), 80.0f, 400.0f);
				Input.m_TargetX = round_to_int(Aim.x);
				Input.m_TargetY = round_to_int(Aim.y);
			}
			if(Kind == 2)
				Input.m_Hook = 1;
			vPath.push_back(pSource->Core()->m_Pos);
			for(int Tick = 0; Tick < Ticks; ++Tick)
			{
				if(Kind == 1)
					Input.m_Fire = (Released + ((Tick == 0 || g_Config.m_NullTasPredictFire) ? 1 : 2)) & INPUT_STATE_MASK;
				StepNullTasWorld(Probe, Tas.m_ClientId, Input);
				const auto *pCharacter = Probe.GetCharacterById(Tas.m_ClientId);
				if(!pCharacter)
					break;
				vPath.push_back(pCharacter->Core()->m_Pos);
				if(Kind == 2 && Tas.m_avPreview[3].empty() && pCharacter->Core()->m_HookState == HOOK_GRABBED)
					Tas.m_avPreview[3].push_back(pCharacter->Core()->m_HookPos);
			}
		}
	}

	void CKrxTas::RenderNullTas()
	{
		if(!NullTasAvailable())
			return;
		auto &Tas = m_NullTas;
		const bool Editor = NullEditorActive();
		const bool SameMap = Tas.m_Map.empty() || Tas.m_Map == GameClient()->Map()->FullName();
		if(!Editor && (!g_Config.m_NullTasDrawPath || !SameMap || Tas.m_vFrames.empty()))
			return;
		const auto OldScreen = Graphics()->GetScreen();
		const auto &Camera = GameClient()->m_Camera;
		Graphics()->MapScreen(Graphics()->MapScreenToWorld(Camera.m_Center.x, Camera.m_Center.y, 100, 100, 100, 0, 0, Graphics()->ScreenAspect(), Camera.m_Zoom));
		if(g_Config.m_NullTasDrawPath && SameMap && !Tas.m_vFrames.empty())
		{
			const size_t Step = std::max<size_t>(1, Tas.m_vFrames.size() / 2500);
			vec2 Previous = Tas.m_vFrames.front().m_Position;
			for(size_t Tick = Step; Tick < Tas.m_vFrames.size(); Tick += Step)
			{
				const vec2 Position = Tas.m_vFrames[Tick].m_Position;
				Line(Previous, Position, ColorRGBA(.32f, .82f, 1.0f, .75f), 2.0f);
				Previous = Position;
			}
			Line(Previous, Tas.m_vFrames.back().m_Position, ColorRGBA(.32f, .82f, 1.0f, .75f), 2.0f);
			Circle(Tas.m_Start, 12.0f, ColorRGBA(.32f, .82f, 1.0f, .8f));
			if(Editor && g_Config.m_NullTasEndRing)
				Circle(Tas.m_vFrames.back().m_Position, 12.0f, ColorRGBA(1.0f, .52f, .2f, .8f));
		}
		if(Editor)
		{
			const auto Now = time_get_nanoseconds().count();
			if(Tas.m_Dirty && !Tas.m_Autosaved && Now - Tas.m_LastAutosave >= 5 * NANOSECONDS)
			{
				Tas.m_LastAutosave = Now;
				WriteNullTas("autosave", true);
			}
			UpdateNullTasPreview();
			const ColorRGBA aPreviewColors[] = {
				ColorRGBA(.32f, .92f, .42f, .72f),
				ColorRGBA(1.0f, .72f, .2f, .72f),
				ColorRGBA(.28f, .92f, .83f, .72f),
			};
			for(size_t Kind = 0; Kind < std::size(aPreviewColors); ++Kind)
				for(size_t Index = 1; Index < Tas.m_avPreview[Kind].size(); ++Index)
					Line(Tas.m_avPreview[Kind][Index - 1], Tas.m_avPreview[Kind][Index], aPreviewColors[Kind], 1.0f);
			if(!Tas.m_avPreview[3].empty())
				Circle(Tas.m_avPreview[3].front(), 5.0f, aPreviewColors[2]);
			auto *pCharacter = Tas.m_pWorld->GetCharacterById(Tas.m_ClientId);
			if(pCharacter && (g_Config.m_NullTasWeaponLaser || KeyHeld(*Input(), g_Config.m_NullTasKeyLaser)))
			{
				vec2 Aim(Tas.m_EditorInput.m_TargetX, Tas.m_EditorInput.m_TargetY);
				if(Tas.m_Boost && Tas.m_BoostUseful)
					Aim = Tas.m_BoostAim;
				if(Aim != vec2(0, 0))
				{
					const vec2 Pos = pCharacter->Core()->m_Pos;
					const vec2 Direction = normalize(Aim);
					if(Tas.m_Weapon >= WEAPON_GUN && Tas.m_Weapon <= WEAPON_LASER)
					{
						for(const auto &Path : NullClient::AimProjectilePath(*Tas.m_pWorld, Tas.m_ClientId, Tas.m_Weapon, Direction, g_Config.m_NullTasPreviewLen))
							for(size_t Index = 1; Index < Path.m_vPoints.size(); ++Index)
								Line(Path.m_vPoints[Index - 1], Path.m_vPoints[Index], aPreviewColors[1], 1.0f);
					}
					else
						Line(Pos, Pos + Direction * 42.0f, aPreviewColors[1], 1.0f);
				}
			}
			Graphics()->MapScreen(CScreenRect(0, 0, 300.0f * Graphics()->ScreenAspect(), 300.0f));
			char aLine[128];
			str_format(aLine, sizeof(aLine), "TAS  |  %zu / %zu  |  %d TPS", Tas.m_Tick, Tas.m_vFrames.size(), g_Config.m_NullTasTps);
			TextRender()->Text(7, 7, 8, aLine);
			if(pCharacter)
			{
				const vec2 Pos = pCharacter->Core()->m_Pos;
				const char *pActivity = NullTasFrozen(*pCharacter) ? "Frozen" : Tas.m_Flowing ? "Running" : "Paused";
				str_format(aLine, sizeof(aLine), "%s  |  %.0f, %.0f  |  Speed %.1f  |  Reload %dt", pActivity, Pos.x, Pos.y,
					length(pCharacter->Core()->m_Vel), pCharacter->DiagnosticReloadTimer());
				TextRender()->Text(7, 17, 6, aLine);
			}
			if(Tas.m_Boost)
			{
				const char *pMove = Tas.m_BoostMove < 0 ? "left" : Tas.m_BoostMove > 0 ? "right" : "neutral";
				str_format(aLine, sizeof(aLine), Tas.m_BoostUseful ? "Boost aim ready (%s)" : "No boost aim available", pMove);
				TextRender()->Text(7, 25, 6, aLine);
			}
		}
		Graphics()->MapScreen(OldScreen);
	}

	void CKrxTas::RefreshNullTasFiles()
	{
		auto &vFiles = m_NullTas.m_vFiles;
		vFiles.clear();
		Storage()->ListDirectory(IStorage::TYPE_ALL, "tas", [](const char *pName, int IsDir, int, void *pUser) {
			if(!IsDir && str_endswith(pName, ".tas") && !NullTasPath(pName).empty())
				static_cast<std::vector<std::string> *>(pUser)->emplace_back(pName);
			return 0;
		}, &vFiles);
		std::sort(vFiles.begin(), vFiles.end());
		vFiles.erase(std::unique(vFiles.begin(), vFiles.end()), vFiles.end());
	}

	bool CKrxTas::WriteNullTas(const char *pName, bool Autosave)
	{
		auto &Tas = m_NullTas;
		const auto Path = NullTasPath(pName);
		const auto *pMap = GameClient()->Map();
		const std::string_view CurrentMap = pMap->FullName();
		if(Path.empty() || Tas.m_vFrames.empty() || (!Autosave && Path == "tas/autosave.tas") ||
			CurrentMap.size() >= MAX_MAP_LENGTH || !pMap->IsLoaded() ||
			(!Tas.m_Map.empty() && Tas.m_Map != CurrentMap))
			return false;
		Storage()->CreateFolder("tas", IStorage::TYPE_SAVE);
		const auto Temporary = Path + ".tmp";
		IOHANDLE File = Storage()->OpenFile(Temporary.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		const bool Wrote = ::OpenKrx::WriteNullTas(File, Tas.m_vFrames, CurrentMap, Tas.m_Start, Tas.m_AllWeapons, *Collision());
		const bool Closed = io_close(File) == 0;
		if(!Wrote || !Closed || !Storage()->RenameFile(Temporary.c_str(), Path.c_str(), IStorage::TYPE_SAVE))
		{
			Storage()->RemoveFile(Temporary.c_str(), IStorage::TYPE_SAVE);
			return false;
		}
		if(Autosave)
			Tas.m_Autosaved = true;
		else
		{
			Tas.m_Dirty = false;
			Tas.m_Name = pName;
		}
		RefreshNullTasFiles();
		return true;
	}

	bool CKrxTas::SaveNullTas(const char *pName)
	{
		if(!NullProfileActive())
			return false;
		const bool Saved = WriteNullTas(pName, false);
		NullTasMessage(Saved ? "Run saved." : "Could not save the run. Check its name and map.");
		return Saved;
	}

	bool CKrxTas::LoadNullTas(const char *pName)
	{
		if(!NullProfileActive())
			return false;
		const auto Path = NullTasPath(pName);
		IOHANDLE File = Path.empty() ? nullptr : Storage()->OpenFile(Path.c_str(), IOFLAG_READ, IStorage::TYPE_ALL);
		CNullTasFile Run;
		bool Loaded = false;
		if(File)
		{
			Loaded = ReadNullTas(File, Run);
			io_close(File);
		}
		if(!Loaded)
		{
			NullTasMessage("Could not read the NTAS run.");
			return false;
		}
		if(NullEditorActive() && !Run.m_Map.empty() && Run.m_Map != GameClient()->Map()->FullName())
		{
			NullTasMessage("This run belongs to a different map.");
			return false;
		}
		const bool Continue = NullEditorActive();
		LeaveNullTasProfile();
		ResetNullTasWorlds();
		auto &Tas = m_NullTas;
		Tas.m_vFrames = std::move(Run.m_vFrames);
		Tas.m_Map = std::move(Run.m_Map);
		Tas.m_Start = Run.m_Start;
		Tas.m_AllWeapons = Run.m_AllWeapons;
		Tas.m_Name = pName;
		Tas.m_Dirty = Path == "tas/autosave.tas";
		Tas.m_Autosaved = Tas.m_Dirty;
		g_Config.m_NullTasAllWeapons = Run.m_AllWeapons;
		if(Continue)
			return StartNullEditor(false);
		NullTasMessage("Run loaded.");
		return true;
	}

	void CKrxTas::InitNullTasCommands()
	{
		Console()->Register("tas_editor", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
			auto &Tas = *static_cast<CKrxTas *>(pUser);
			if(Tas.NullEditorActive())
				Tas.StopNullEditor();
			else
				Tas.StartNullEditor(!Tas.m_NullTas.m_pWorld && Tas.m_NullTas.m_vFrames.empty());
		}, this, "Open or close the Null TAS editor");
		Console()->Register("null_tas_save", "s[name]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
			static_cast<CKrxTas *>(pUser)->SaveNullTas(pResult->GetString(0));
		}, this, "Save the current Null TAS run");
		Console()->Register("null_tas_load", "s[name]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
			static_cast<CKrxTas *>(pUser)->LoadNullTas(pResult->GetString(0));
		}, this, "Load a local NTAS run");
		Console()->Register("null_tas_play", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
			static_cast<CKrxTas *>(pUser)->ToggleNullPlayback();
		}, this, "Start or stop playback of the loaded Null TAS run");
	}
}
