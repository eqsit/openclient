#include "settings.h"

#include <base/str.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <string>
#include <string_view>

namespace OpenKrx::NullClient
{
	namespace
	{
		struct CAvoidOption
		{
			int CConfig::*m_pValue;
			int m_aDefaults[3];
			int m_Minimum;
			int m_Maximum;
		};

		// This order is the on-disk version-one preset format.
		constexpr CAvoidOption s_aAvoidOptions[] = {
			{&CConfig::m_NullGodmodeHorizon, {25, 32, 40}, 5, 200},
			{&CConfig::m_NullGodmodeReact, {11, 13, 15}, 1, 200},
			{&CConfig::m_NullGodmodePlanHorizon, {8, 9, 10}, 2, 60},
			{&CConfig::m_NullGodmodeDeep, {0, 0, 0}, 0, 1},
			{&CConfig::m_NullGodmodeDeepHorizon, {20, 20, 20}, 10, 60},
			{&CConfig::m_NullGodmodeDeepChain, {6, 6, 6}, 0, 12},
			{&CConfig::m_NullGodmodeDeviation, {300, 250, 200}, 0, 5000},
			{&CConfig::m_NullGodmodeHold, {10, 15, 15}, 0, 200},
			{&CConfig::m_NullGodmodeHysteresis, {10, 12, 15}, 0, 200},
			{&CConfig::m_NullGodmodeAnchorSpeed, {500, 500, 500}, 0, 4000},
			{&CConfig::m_NullGodmodeAnchorLeash, {120, 120, 120}, 16, 1000},
			{&CConfig::m_NullGodmodeContinuity, {120, 120, 120}, 0, 2000},
			{&CConfig::m_NullGodmodeHoldSlack, {24, 24, 24}, 0, 400},
			{&CConfig::m_NullGodmodeSettle, {40, 40, 40}, 0, 2000},
			{&CConfig::m_NullGodmodeFreezeCost, {9000, 15000, 20000}, 0, 100000},
			{&CConfig::m_NullGodmodeSimRadius, {1200, 1200, 1200}, 0, 4000},
			{&CConfig::m_NullGodmodeAdversarial, {1, 1, 1}, 0, 2},
			{&CConfig::m_NullGodmodePlayerRadius, {2000, 2000, 2000}, 0, 8000},
			{&CConfig::m_NullGodmodeReactHooked, {12, 12, 12}, 0, 100},
			{&CConfig::m_NullGodmodeHookAngles, {12, 12, 12}, 8, 128},
			{&CConfig::m_NullGodmodeLetGo, {1, 1, 1}, 0, 1},
			{&CConfig::m_NullGodmodeCutHold, {5, 5, 5}, 0, 40},
			{&CConfig::m_NullGodmodeCutTicks, {0, 0, 0}, 0, 200},
			{&CConfig::m_NullGodmodeCutWindow, {0, 0, 0}, 0, 200},
			{&CConfig::m_NullGodmodeTravel, {300, 300, 300}, 0, 5000},
			{&CConfig::m_NullGodmodeHandBack, {0, 0, 0}, 0, 1},
			{&CConfig::m_NullGodmodeLastMoment, {1, 1, 1}, 0, 1},
			{&CConfig::m_NullGodmodeGrace, {1, 2, 3}, 1, 40},
			{&CConfig::m_NullGodmodeVetoFire, {14, 14, 14}, 0, 60},
			{&CConfig::m_NullGodmodeVetoTicks, {2, 3, 3}, 0, 20},
			{&CConfig::m_NullGodmodeReactSpeed, {30, 30, 30}, 0, 400},
			{&CConfig::m_NullGodmodeReactShock, {100, 100, 100}, 0, 400},
			{&CConfig::m_NullGodmodeAntiknockback, {250, 250, 250}, 100, 2000},
			{&CConfig::m_NullGodmodeAirJump, {7000, 5000, 5000}, 0, 50000},
			{&CConfig::m_NullGodmodeFollow, {10, 10, 10}, 0, 40},
			{&CConfig::m_NullGodmodeCommitment, {1600, 1500, 2000}, 0, 20000},
			{&CConfig::m_NullGodmodeHookCost, {1300, 900, 900}, 0, 20000},
			{&CConfig::m_NullGodmodeHookAge, {20, 25, 25}, 0, 2000},
			{&CConfig::m_NullGodmodeWallCost, {40, 40, 40}, 0, 2000},
			{&CConfig::m_NullGodmodeTeeHookCost, {1500, 1500, 1500}, 0, 20000},
			{&CConfig::m_NullGodmodeChain, {0, 0, 0}, 0, 12},
			{&CConfig::m_NullGodmodeCoast, {10, 10, 10}, 0, 60},
		};
		using CAvoidValues = std::array<int, std::size(s_aAvoidOptions)>;

		void ApplyAvoidDefaults(int Mode)
		{
			for(const auto &Option : s_aAvoidOptions)
				g_Config.*Option.m_pValue = Option.m_aDefaults[Mode - 1];
		}

		CAvoidValues ReadAvoidValues()
		{
			CAvoidValues aValues;
			for(size_t Index = 0; Index < aValues.size(); ++Index)
				aValues[Index] = g_Config.*s_aAvoidOptions[Index].m_pValue;
			return aValues;
		}

		bool ParseAvoidValues(std::string_view Text, int Mode, CAvoidValues &aValues)
		{
			for(size_t Index = 0; Index < aValues.size(); ++Index)
				aValues[Index] = s_aAvoidOptions[Index].m_aDefaults[Mode - 1];
			if(!Text.starts_with("1:") || Text.size() == 2)
				return false;
			Text.remove_prefix(2);
			for(size_t Index = 0; !Text.empty(); ++Index)
			{
				if(Index == aValues.size())
					return false;
				const auto End = Text.find(',');
				const auto Field = Text.substr(0, End);
				int Value = 0;
				const auto Parsed = std::from_chars(Field.data(), Field.data() + Field.size(), Value);
				if(Parsed.ec != std::errc{} || Parsed.ptr != Field.data() + Field.size())
					return false;
				const auto &Option = s_aAvoidOptions[Index];
				aValues[Index] = std::clamp(Value, Option.m_Minimum, Option.m_Maximum);
				Text.remove_prefix(End == std::string_view::npos ? Text.size() : End + 1);
			}
			return true;
		}

		// 0x140219910 keeps three independent banks. Each field has its own capacity,
		// so every write is bounded by the destination and not by a sibling field (A9-014).
		struct CAvoidBank
		{
			char *m_pText;
			int m_Size;
		};

		CAvoidBank AvoidBank(int Mode)
		{
			if(Mode == 1)
				return {g_Config.m_NullAvoidBoostSettings, (int)sizeof(g_Config.m_NullAvoidBoostSettings)};
			if(Mode == 2)
				return {g_Config.m_NullAvoidOverdriveSettings, (int)sizeof(g_Config.m_NullAvoidOverdriveSettings)};
			return {g_Config.m_NullAvoidGodSettings, (int)sizeof(g_Config.m_NullAvoidGodSettings)};
		}

		void StoreAvoidValues(const CAvoidBank &Bank, const CAvoidValues &aValues)
		{
			std::string Text = "1:";
			for(size_t Index = 0; Index < aValues.size(); ++Index)
			{
				if(Index != 0)
					Text += ',';
				Text += std::to_string(aValues[Index]);
			}
			str_copy(Bank.m_pText, Text.c_str(), Bank.m_Size);
		}
	}

	// 0x140219ea0 detects external preset edits as well as mode changes.
	void SyncAvoidModeSettings(bool KeepCurrentValues)
	{
		static int s_PreviousMode = -1;
		static std::array<CAvoidValues, 4> s_aValues;
		static std::array<std::string, 4> s_aPreviousBanks;
		const int Mode = std::clamp(g_Config.m_NullAvoidMode, 0, 3);
		bool Reload = s_PreviousMode < 0;
		for(int Index = 1; Index <= 3; ++Index)
			Reload |= s_aPreviousBanks[Index] != AvoidBank(Index).m_pText;
		bool WriteBanks = Reload;
		if(Reload)
		{
			for(int Index = 1; Index <= 3; ++Index)
			{
				const char *pText = AvoidBank(Index).m_pText;
				auto &Values = s_aValues[Index];
				if(!*pText && Index == 3 && Mode == 3)
					Values = ReadAvoidValues();
				else if(!ParseAvoidValues(pText, Index, Values))
					for(size_t Field = 0; Field < Values.size(); ++Field)
						Values[Field] = s_aAvoidOptions[Field].m_aDefaults[Index - 1];
			}
		}
		const int EditedMode = KeepCurrentValues ? Mode : s_PreviousMode;
		if(EditedMode > 0 && (!Reload || KeepCurrentValues))
		{
			const auto Values = ReadAvoidValues();
			if(s_aValues[EditedMode] != Values)
			{
				s_aValues[EditedMode] = Values;
				WriteBanks = true;
			}
		}
		if(Mode > 0 && (Reload || Mode != s_PreviousMode))
			for(size_t Field = 0; Field < s_aValues[Mode].size(); ++Field)
				g_Config.*s_aAvoidOptions[Field].m_pValue = s_aValues[Mode][Field];
		if(WriteBanks)
			for(int Index = 1; Index <= 3; ++Index)
			{
				const CAvoidBank Bank = AvoidBank(Index);
				StoreAvoidValues(Bank, s_aValues[Index]);
				s_aPreviousBanks[Index] = Bank.m_pText;
			}
		s_PreviousMode = Mode;
	}

	void ResetSelectedAvoidMode()
	{
		SyncAvoidModeSettings();
		const int Mode = std::clamp(g_Config.m_NullAvoidMode, 0, 3);
		if(Mode == 0)
			g_Config.m_NullAvoidLegitHook = 0;
		else
		{
			ApplyAvoidDefaults(Mode);
			StoreAvoidValues(AvoidBank(Mode), ReadAvoidValues());
		}
		SyncAvoidModeSettings(true);
	}

	bool IsAvoidModeOption(int CConfig::*pValue)
	{
		return std::any_of(std::begin(s_aAvoidOptions), std::end(s_aAvoidOptions), [pValue](const auto &Option) {
			return Option.m_pValue == pValue;
		});
	}
	CSettings SettingsFromConfig()
	{
		SyncAvoidModeSettings();
		CSettings Settings;
		Settings.m_Enabled = g_Config.m_NullGodmode;
		Settings.m_Mode = g_Config.m_NullAvoidMode;
		Settings.m_LegitHook = g_Config.m_NullAvoidLegitHook;
		Settings.m_Dummy = g_Config.m_NullGodmodeDummy;
		Settings.m_Horizon = g_Config.m_NullGodmodeHorizon;
		Settings.m_React = g_Config.m_NullGodmodeReact;
		Settings.m_PlanHorizon = g_Config.m_NullGodmodePlanHorizon;
		Settings.m_Deep = g_Config.m_NullGodmodeDeep;
		Settings.m_DeepHorizon = g_Config.m_NullGodmodeDeepHorizon;
		Settings.m_DeepChain = g_Config.m_NullGodmodeDeepChain;
		Settings.m_Deviation = g_Config.m_NullGodmodeDeviation;
		Settings.m_Hold = g_Config.m_NullGodmodeHold;
		Settings.m_Hysteresis = g_Config.m_NullGodmodeHysteresis;
		Settings.m_AnchorSpeed = g_Config.m_NullGodmodeAnchorSpeed;
		Settings.m_AnchorLeash = g_Config.m_NullGodmodeAnchorLeash;
		Settings.m_Continuity = g_Config.m_NullGodmodeContinuity;
		Settings.m_HoldSlack = g_Config.m_NullGodmodeHoldSlack;
		Settings.m_Settle = g_Config.m_NullGodmodeSettle;
		Settings.m_FreezeCost = g_Config.m_NullGodmodeFreezeCost;
		Settings.m_SimRadius = g_Config.m_NullGodmodeSimRadius;
		Settings.m_Adversarial = g_Config.m_NullGodmodeAdversarial;
		Settings.m_PlayerRadius = g_Config.m_NullGodmodePlayerRadius;
		Settings.m_ReactHooked = g_Config.m_NullGodmodeReactHooked;
		Settings.m_HookAngles = g_Config.m_NullGodmodeHookAngles;
		Settings.m_Indicator = g_Config.m_NullGodmodeIndicator;
		Settings.m_Trace = g_Config.m_NullGodmodeTrace;
		Settings.m_Debug = g_Config.m_NullGodmodeDebug;
		Settings.m_LetGo = g_Config.m_NullGodmodeLetGo;
		Settings.m_CutHold = g_Config.m_NullGodmodeCutHold;
		Settings.m_CutTicks = g_Config.m_NullGodmodeCutTicks;
		Settings.m_CutWindow = g_Config.m_NullGodmodeCutWindow;
		Settings.m_Travel = g_Config.m_NullGodmodeTravel;
		Settings.m_HandBack = g_Config.m_NullGodmodeHandBack;
		Settings.m_LastMoment = g_Config.m_NullGodmodeLastMoment;
		Settings.m_VetoHook = g_Config.m_NullGodmodeVetoHook;
		Settings.m_Grace = g_Config.m_NullGodmodeGrace;
		Settings.m_VetoFire = g_Config.m_NullGodmodeVetoFire;
		Settings.m_VetoTicks = g_Config.m_NullGodmodeVetoTicks;
		Settings.m_VetoDoom = g_Config.m_NullGodmodeVetoDoom;
		Settings.m_ReactSpeed = g_Config.m_NullGodmodeReactSpeed;
		Settings.m_ReactSpeedFast = g_Config.m_NullGodmodeReactSpeedFast;
		Settings.m_ReactShock = g_Config.m_NullGodmodeReactShock;
		Settings.m_HammerDodge = g_Config.m_NullGodmodeHammerDodge;
		Settings.m_HammerDodgeMode = g_Config.m_NullGodmodeHammerDodgeMode;
		Settings.m_HammerLookahead = g_Config.m_NullGodmodeHammerLookahead;
		Settings.m_RocketDodge = g_Config.m_NullGodmodeRocketDodge;
		Settings.m_RocketRange = g_Config.m_NullGodmodeRocketRange;
		Settings.m_RocketCost = g_Config.m_NullGodmodeRocketCost;
		Settings.m_RocketThrow = g_Config.m_NullGodmodeRocketThrow;
		Settings.m_RocketSpread = g_Config.m_NullGodmodeRocketSpread;
		Settings.m_RocketNear = g_Config.m_NullGodmodeRocketNear;
		Settings.m_RocketLookahead = g_Config.m_NullGodmodeRocketLookahead;
		Settings.m_RocketAnswer = g_Config.m_NullGodmodeRocketAnswer;
		Settings.m_RocketContact = g_Config.m_NullGodmodeAvoidRocket;
		Settings.m_RocketContactMode = g_Config.m_NullGodmodeAvoidRocketMode;
		Settings.m_RocketContactLookahead = g_Config.m_NullGodmodeAvoidRocketLookahead;
		Settings.m_RocketContactCost = g_Config.m_NullGodmodeAvoidRocketCost;
		Settings.m_HammerCost = g_Config.m_NullGodmodeHammerCost;
		Settings.m_HammerRing = g_Config.m_NullGodmodeHammerRing;
		Settings.m_Knockback = g_Config.m_NullGodmodeKnockback;
		Settings.m_KnockbackMode = g_Config.m_NullGodmodeKnockbackMode;
		Settings.m_KnockbackGrip = g_Config.m_NullGodmodeKnockbackGrip;
		Settings.m_KnockbackHold = g_Config.m_NullGodmodeKnockbackHold;
		Settings.m_KnockbackClearance = g_Config.m_NullGodmodeKnockbackClearance;
		Settings.m_Antiknockback = g_Config.m_NullGodmodeAntiknockback;
		Settings.m_HookCounter = g_Config.m_NullGodmodeHookCounter;
		Settings.m_HookCounterGap = g_Config.m_NullGodmodeHookCounterGap;
		Settings.m_HookCounterPull = g_Config.m_NullGodmodeHookCounterPull;
		Settings.m_HookCounterArc = g_Config.m_NullGodmodeHookCounterArc;
		Settings.m_Weapons = g_Config.m_NullGodmodeWeapons;
		Settings.m_WeaponRadius = g_Config.m_NullGodmodeWeaponRadius;
		Settings.m_ShotPath = g_Config.m_NullGodmodeShotPath;
		Settings.m_ShotPathTicks = g_Config.m_NullGodmodeShotPathTicks;
		Settings.m_Grenade = g_Config.m_NullGodmodeGrenade;
		Settings.m_Hammer = g_Config.m_NullGodmodeHammer;
		Settings.m_AutoWeapon = g_Config.m_NullGodmodeAutoWeapon;
		Settings.m_RestoreWeapon = g_Config.m_NullGodmodeRestoreWeapon;
		Settings.m_Laser = g_Config.m_NullGodmodeLaser;
		Settings.m_AirJump = g_Config.m_NullGodmodeAirJump;
		Settings.m_Follow = g_Config.m_NullGodmodeFollow;
		Settings.m_Commitment = g_Config.m_NullGodmodeCommitment;
		Settings.m_HookCost = g_Config.m_NullGodmodeHookCost;
		Settings.m_HookAge = g_Config.m_NullGodmodeHookAge;
		Settings.m_WallCost = g_Config.m_NullGodmodeWallCost;
		Settings.m_TeeHookCost = g_Config.m_NullGodmodeTeeHookCost;
		Settings.m_Chain = g_Config.m_NullGodmodeChain;
		Settings.m_Coast = g_Config.m_NullGodmodeCoast;
		Settings.m_HazardSpeed = g_Config.m_NullGodmodeHazardSpeed;
		return Settings;
	}
}
