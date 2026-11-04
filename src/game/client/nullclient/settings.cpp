#include "settings.h"

#include <engine/shared/config.h>

namespace OpenKrx::NullClient
{
	CSettings SettingsFromConfig()
	{
		CSettings Settings;
		Settings.m_Enabled = g_Config.m_NullGodmode;
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
		Settings.m_CutWindow = g_Config.m_NullGodmodeCutWindow;
		Settings.m_Travel = g_Config.m_NullGodmodeTravel;
		Settings.m_HandBack = g_Config.m_NullGodmodeHandBack;
		Settings.m_LastMoment = g_Config.m_NullGodmodeLastMoment;
		Settings.m_VetoHook = g_Config.m_NullGodmodeVetoHook;
		Settings.m_Grace = g_Config.m_NullGodmodeGrace;
		Settings.m_VetoFire = g_Config.m_NullGodmodeVetoFire;
		Settings.m_VetoTicks = g_Config.m_NullGodmodeVetoTicks;
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
