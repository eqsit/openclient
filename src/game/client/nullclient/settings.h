#ifndef GAME_CLIENT_NULLCLIENT_SETTINGS_H
#define GAME_CLIENT_NULLCLIENT_SETTINGS_H

namespace OpenKrx::NullClient
{
	struct CSettings
	{
		int m_Enabled = 0;
		int m_Dummy = 0;
		int m_Horizon = 40;
		int m_React = 15;
		int m_PlanHorizon = 10;
		int m_Deep = 0;
		int m_DeepHorizon = 20;
		int m_DeepChain = 6;
		int m_Deviation = 200;
		int m_Hold = 15;
		int m_Hysteresis = 15;
		int m_AnchorSpeed = 500;
		int m_AnchorLeash = 120;
		int m_Continuity = 120;
		int m_HoldSlack = 24;
		int m_Settle = 40;
		int m_FreezeCost = 20000;
		int m_SimRadius = 1200;
		int m_Adversarial = 1;
		int m_PlayerRadius = 2000;
		int m_ReactHooked = 12;
		int m_HookAngles = 12;
		int m_Indicator = 1;
		int m_Trace = 1;
		int m_Debug = 0;
		int m_LetGo = 1;
		int m_CutHold = 5;
		int m_CutWindow = 0;
		int m_Travel = 300;
		int m_HandBack = 0;
		int m_LastMoment = 1;
		int m_VetoHook = 12;
		int m_Grace = 3;
		int m_VetoFire = 14;
		int m_VetoTicks = 3;
		int m_ReactSpeed = 30;
		int m_ReactSpeedFast = 30;
		int m_ReactShock = 100;
		int m_HammerDodge = 1;
		int m_HammerDodgeMode = 0;
		int m_HammerLookahead = 14;
		int m_RocketDodge = 0;
		int m_RocketRange = 600;
		int m_RocketCost = 3000;
		int m_RocketThrow = 170;
		int m_RocketSpread = 70;
		int m_RocketNear = 220;
		int m_RocketLookahead = 25;
		int m_RocketAnswer = 1;
		int m_HammerCost = 1200;
		int m_HammerRing = 1;
		int m_Knockback = 1;
		int m_KnockbackMode = 1;
		int m_KnockbackGrip = 250;
		int m_KnockbackHold = 50;
		int m_KnockbackClearance = 60;
		int m_Antiknockback = 250;
		int m_HookCounter = 0;
		int m_HookCounterGap = 48;
		int m_HookCounterPull = 300;
		int m_Weapons = 1;
		int m_WeaponRadius = 1600;
		int m_ShotPath = 1;
		int m_ShotPathTicks = 40;
		int m_Grenade = 1;
		int m_Hammer = 1;
		int m_AutoWeapon = 1;
		int m_RestoreWeapon = 0;
		int m_Laser = 0;
		int m_AirJump = 5000;
		int m_Follow = 10;
		int m_Commitment = 2000;
		int m_HookCost = 900;
		int m_HookAge = 25;
		int m_WallCost = 40;
		int m_TeeHookCost = 1500;
		int m_Chain = 0;
		int m_Coast = 10;
		int m_HazardSpeed = 0;
	};
	CSettings SettingsFromConfig();
}

#endif
