#ifndef GAME_CLIENT_COMPONENTS_KRX_TAS_H
#define GAME_CLIENT_COMPONENTS_KRX_TAS_H

#include <base/color.h>

#include <game/client/component.h>
#include <game/client/krx/flow_field.h>
#include <game/client/krx/replay.h>
#include <game/client/krx/replay_analyzer.h>
#include <game/client/krx/replay_vault.h>
#include <game/client/prediction/gameworld.h>

#include <array>
#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

class IHttpRequest;

namespace OpenKrx
{
	struct CNullTasFrame
	{
		CNetObj_PlayerInput m_Input{};
		vec2 m_Position = vec2(0, 0);
		bool m_Frozen = false;
	};

	class CKrxTas : public CComponent
	{
	public:
		int Sizeof() const override { return sizeof(*this); }
		~CKrxTas() override;
		void OnInit() override
		{
			RefreshFiles();
			RefreshNullTasFiles();
		}
		void OnReset() override;
		void OnMapLoad() override;
		void OnRender() override;
		void OnConsoleInit() override;
		bool OnInput(const IInput::CEvent &Event) override;
		void OnShutdown() override;
		// Called once at the end of the original SnapInput pipeline, before send.
		bool ProcessInput();
		CNetObj_PlayerInput NetworkInput() const;
		bool Active() const;
		CGameWorld *World();
		CGameWorld *RenderWorld();
		CGameWorld *PreviousRenderWorld();
		float IntraTick() const;
		void ApplyRenderState();
		void ClearReplay();
		bool SaveReplay(const char *pName, bool Auto = false, bool Overwrite = false);
		bool LoadReplay(const char *pName);
		bool DeleteReplay(const char *pName);
		bool ValidateReplay();
		int GetReplayTime();
		int LastReplayTimeTicks() const { return m_LastReplayTimeTicks; }
		int FinishTicks() const { return m_ReplayFinishTicks; }
		void ConvertLegacyReplays();
		bool AddFakeAim();
		const CReplay &Replay() const { return m_Replay; }
		const std::string &Status() const { return m_Status; }
		const std::vector<std::string> &ReplayFiles() const { return m_vReplayFiles; }
		void RefreshFiles();
		void SyncVault();
		void CheckAutoSyncVault();
		void Rewind(int Ticks);
		void Forward(int Ticks);
		void PrepareForFent();
		void ImportInputs(const CGameWorld &Initial, int Id, const std::vector<CNetObj_PlayerInput> &vInputs);
		void UpdatePathFinding();
		const CFlowField &FlowField() const { return m_FlowField; }
		void SetGoal(vec2 Goal);
		void AnalyzeReplay();
		const std::vector<unsigned char> &EditorTiles() const { return m_vTunnels; }
		bool NullEditorActive() const;
		bool StartNullEditor(bool NewRun);
		void StopNullEditor();
		bool SaveNullTas(const char *pName);
		bool LoadNullTas(const char *pName);
		void ToggleNullPlayback();
		bool NullPlaybackActive() const;
		bool NullTasInputActive() const;
		const std::string &NullTasStatus() const { return m_NullTas.m_Status; }
		struct CNullTasFileEntry
		{
			std::string m_Name, m_Label;
		};
		const std::vector<CNullTasFileEntry> &NullTasFiles() const { return m_NullTas.m_vFiles; }
		void RefreshNullTasFiles();
		int NullTasFrameCount() const { return static_cast<int>(m_NullTas.m_vFrames.size()); }
		int NullTasCursor() const { return static_cast<int>(m_NullTas.m_Tick); }
		void SeekNullTas(size_t Tick, bool UpdatePositions = false);
		void LeaveNullTasProfile();
		bool ProcessNullTasInput(const CNetObj_PlayerInput &Raw, const CNetObj_PlayerInput &Last, CNetObj_PlayerInput &Packet);

	private:
		bool KrxEditorActive() const;
		struct CNullTasState
		{
			std::unique_ptr<CCollision> m_pCollision;
			std::array<CTuningParams, 256> m_aTuning;
			std::unique_ptr<CGameWorld> m_pWorld, m_pPreviousWorld, m_pInitialWorld;
			std::vector<std::unique_ptr<CGameWorld>> m_vCheckpoints;
			std::vector<CNullTasFrame> m_vFrames;
			std::vector<CNullTasFileEntry> m_vFiles;
			std::string m_Map, m_Name, m_Status;
			vec2 m_Start = vec2(0, 0);
			std::array<bool, 5> m_aInitialWeaponGot{};
			std::array<int, 5> m_aInitialWeaponAmmo{};
			CNetObj_PlayerInput m_EditorInput{}, m_PlaybackInput{};
			size_t m_Tick = 0, m_PlaybackTick = 0;
			int m_ClientId = -1, m_Slot = -1, m_LastPlaybackTick = -1;
			int m_StartTick = -1, m_FinishTick = -1;
			int m_Coast = 0, m_Seek = 0, m_Weapon = -1, m_FireState = 0;
			int64_t m_LastStep = 0, m_LastAutosave = 0, m_LastBoost = 0, m_LastPreview = 0;
			int64_t m_StepTime = 0;
			bool m_Editor = false, m_Playback = false, m_PlaybackAligned = false;
			bool m_Dirty = false, m_Autosaved = false, m_Boost = false;
			bool m_AllWeapons = false, m_Flowing = false, m_BoostUseful = false;
			vec2 m_BoostReference = vec2(0, 0), m_BoostAim = vec2(0, 0);
			int m_BoostMove = 0;
			std::array<std::vector<vec2>, 4> m_avPreview;
		} m_NullTas;
		bool NullTasAvailable() const;
		void NullTasMessage(const char *pMessage);
		void InitNullTasCommands();
		void ResetNullTas();
		void ResetNullTasWorlds();
		bool WriteNullTas(const char *pName, bool Autosave);
		void SetNullTasWeapon(int Weapon);
		void UpdateNullEditor(CNetObj_PlayerInput Raw);
		void UpdateNullTasBoost(vec2 Aim);
		void UpdateNullTasPreview();
		void UpdateNullPlayback(const CNetObj_PlayerInput &Raw, const CNetObj_PlayerInput &Last, CNetObj_PlayerInput &Packet);
		static void StepNullTasWorld(CGameWorld &World, int ClientId, const CNetObj_PlayerInput &Input);
		void RenderNullTas();

		CReplay m_Replay;
		std::unique_ptr<CGameWorld> m_pWorld, m_pPreviousWorld, m_pInitialWorld;
		std::vector<std::shared_ptr<CGameWorld>> m_vWorlds;
		std::array<int, 2> m_aIds = {-1, -1};
		// A loaded replay_local controls the currently active connection (2f2cf0).
		std::array<int, 2> m_aReplayInputSlots = {0, 1};
		std::array<CNetObj_PlayerInput, 2> m_aInputs{};
		CNetObj_PlayerInput m_NetworkBefore{};
		std::vector<std::string> m_vReplayFiles;
		std::future<int> m_ConversionFuture;
		std::atomic<bool> m_ConversionCancel{false};
		std::future<CReplayStabilityResult> m_AnalysisFuture;
		std::atomic<bool> m_AnalysisCancel{false};
		std::string m_Status, m_OverwriteName;
		bool m_WasRecording = false, m_RewindHeld = false, m_ForwardHeld = false;
		bool m_PlaybackWasActive = false;
		int m_PlaybackTick = 0, m_LastPlaybackGameTick = -1;
		int64_t m_LastStep = 0, m_LastAutoSave = 0;
		int m_StartTick = 0, m_FinishTick = 0;
		int m_ReplayFinishTicks = 0;
		int m_LastReplayTimeTicks = 0;
		CFlowField m_FlowField;
		std::vector<unsigned char> m_vTunnels;
		vec2 m_Goal = vec2(0, 0), m_LastBrush = vec2(0, 0);
		bool m_PathDirty = true;
		bool m_NoFinish = false;
		bool m_EditorBrushValid = false;
		struct CPathResult
		{
			CFlowField m_Field;
			std::vector<double> m_vBenchmarkUs;
		};
		std::future<CPathResult> m_PathFuture;
		std::shared_ptr<std::atomic<bool>> m_pPathCancel;
		int m_LastLightTiles = -1, m_LastLightRadius = -1;
		std::shared_ptr<IHttpRequest> m_pVaultRequest;
		std::string m_VaultSha;
		int m_VaultStage = 0;
		bool m_VaultAutoChecked = false;
		struct CVaultResult
		{
			std::vector<CVaultFile> m_vFiles;
			std::string m_Error;
		};
		std::future<CVaultResult> m_VaultFuture;
		std::vector<CVaultFile> m_vVaultPending;
		size_t m_VaultNextFile = 0;
		void CancelPathFinding();
		void StopVaultWorker();
		void StopConversionWorker();
		void StopAnalysisWorker();
		void UpdateAnalysis();
		void UpdateLegacyConversion();
		void InstallVaultFiles();
		void UpdateVault();
		void InitializeWorld(const CGameWorld &Source, bool PredictPlayers);
		void Step(const std::array<CNetObj_PlayerInput, 2> &aInputs, bool Record, bool Events, int InputCount = 2, unsigned InputMask = 3);
		void AppendFrame(const std::array<CNetObj_PlayerInput, 2> &aInputs);
		void Playback();
		void RenderPath(const std::vector<CReplayFrame> &vFrames, unsigned Color, int Mode, bool Segmented);
		void RenderPrediction();
		void RenderFlowField();
		void RenderHud();
		void Line(vec2 From, vec2 To, ColorRGBA Color, float Width = 5);
		void Circle(vec2 Pos, float Radius, ColorRGBA Color);
		void Message(const char *pMessage);
		void HandleWorldEvents();
		void AutoRewind();
		bool MakePath(const char *pName, std::string &Path, bool Auto) const;
		bool PathTileBlocked(int X, int Y, const std::vector<unsigned char> &vLight) const;
		void AutoTunnel();
		void AutoFinish();
	};
}
#endif
