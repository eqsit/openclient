#include "menus.h"

#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/ui.h>
#include <game/client/gameclient.h>
#include <game/client/components/key_binder.h>
#include <game/client/ui_scrollregion.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <initializer_list>
#include <map>

void CMenus::RenderSettingsNullClient(CUIRect MainView)
{
	// Null draws its own controls. Scope out the host KRX neutral-fill color
	// adapter without changing the user's saved theme or the KRX pages.
	struct CThemeScope
	{
		int m_Saved = g_Config.m_KrxCustomTheme;
		CThemeScope() { g_Config.m_KrxCustomTheme = 0; }
		~CThemeScope() { g_Config.m_KrxCustomTheme = m_Saved; }
	} ThemeScope;
	// Null 3.6: 1402267d0 (page), 1402208b0 (cards), 140225290 (rows).
	// Registration variables are not a menu inventory: Survival has three
	// top-level cards; six dependent cards appear when Avoid is open and on.
	struct CSetting
	{
		int *m_pValue;
		const char *m_pLabel;
		int m_Min = 0, m_Max = 1;
		const char *m_pSuffix = "";
		int m_Choice = 0;
		std::initializer_list<const char *> m_Options;
		bool m_Key = false;
		unsigned *m_pColor = nullptr;
		char *m_pText = nullptr;
		size_t m_TextSize = 0;
	};
	struct CCard
	{
		const char *m_pTitle;
		const char *m_pDescription;
		int *m_pEnabled;
		bool m_Child;
		bool m_AlwaysOpen;
		std::initializer_list<CSetting> m_Settings;
		int m_Page = 0;
		int m_Column = 0;
	};
#define S(Name, Label, Min, Max, Suffix) {&g_Config.m_NullGodmode##Name, Label, Min, Max, Suffix}
#define B(Name, Label) S(Name, Label, 0, 1, "")
	const int AimWeapon = std::clamp(g_Config.m_NullAimbotWeaponSel, 0, 5);
	int *apAimEnabled[] = {&g_Config.m_NullAimbotHook, &g_Config.m_NullAimbotHammer, &g_Config.m_NullAimbotGun, &g_Config.m_NullAimbotShotgun, &g_Config.m_NullAimbotGrenade, &g_Config.m_NullAimbotLaser};
	int *apAimFov[] = {&g_Config.m_NullAimbotFovHook, &g_Config.m_NullAimbotFovHammer, &g_Config.m_NullAimbotFovGun, &g_Config.m_NullAimbotFovShotgun, &g_Config.m_NullAimbotFovGrenade, &g_Config.m_NullAimbotFovLaser};
	int *apAimPriority[] = {&g_Config.m_NullAimbotPriorityHook, &g_Config.m_NullAimbotPriorityHammer, &g_Config.m_NullAimbotPriorityGun, &g_Config.m_NullAimbotPriorityShotgun, &g_Config.m_NullAimbotPriorityGrenade, &g_Config.m_NullAimbotPriorityLaser};
	int *apAimDraw[] = {&g_Config.m_NullAimbotDrawHook, &g_Config.m_NullAimbotDrawHammer, &g_Config.m_NullAimbotDrawGun, &g_Config.m_NullAimbotDrawShotgun, &g_Config.m_NullAimbotDrawGrenade, &g_Config.m_NullAimbotDrawLaser};
	int *apAimAlpha[] = {&g_Config.m_NullAimbotAlphaHook, &g_Config.m_NullAimbotAlphaHammer, &g_Config.m_NullAimbotAlphaGun, &g_Config.m_NullAimbotAlphaShotgun, &g_Config.m_NullAimbotAlphaGrenade, &g_Config.m_NullAimbotAlphaLaser};
	int *apAimKnock[] = {nullptr, nullptr, &g_Config.m_NullAimbotKnockGun, &g_Config.m_NullAimbotKnockShotgun, &g_Config.m_NullAimbotKnockGrenade, &g_Config.m_NullAimbotKnockLaser};
	const CCard aCards[] = {
		{"Avoid Freeze", "Automatically saves you at the last possible moment.", &g_Config.m_NullGodmode, false, false, {S(React, "Activate when danger is within", 1, 200, " ticks"), S(Hysteresis, "Return control once safer by", 0, 200, " ticks"), S(Horizon, "Watch your input for", 5, 200, " ticks"), B(LastMoment, "Wait for the last tick that can save you"), S(Grace, "Minimum safety margin", 1, 40, " ticks"), S(FreezeCost, "Penalty per frozen tick", 0, 100000, "")}},
		{"Hook release", "Releases a hook that is dragging you into danger.", &g_Config.m_NullGodmodeLetGo, true, false, {S(CutHold, "Hold the release for", 0, 40, " ticks"), S(CutWindow, "Evaluate a release over", 0, 200, " ticks")}},
		{"Position hold", "The spot it defends, and how far you can carry it with you.", nullptr, true, false, {S(Hold, "Hold position", 0, 200, "%"), S(HoldSlack, "Allowed drift radius", 0, 400, ""), S(AnchorSpeed, "Your keys move it at", 0, 4000, ""), S(AnchorLeash, "Maximum lead distance", 16, 1000, "")}},
		{"Reflexes", "How much earlier it reacts when something affects you.", nullptr, true, false, {S(ReactSpeed, "React earlier at high speed", 0, 400, "%"), S(ReactSpeedFast, "React even earlier at very high speed", 0, 800, ""), S(ReactShock, "React earlier when hit", 0, 400, "%"), S(ReactHooked, "React earlier while hooked", 0, 100, " ticks"), S(Antiknockback, "Resist being dragged", 100, 2000, "%"), S(VetoTicks, "Refuse a step off the edge within", 0, 20, " ticks"), S(VetoHook, "Refuse a bad hook within", 0, 60, " ticks"), S(VetoFire, "Check your own grenade before firing, within", 0, 60, " ticks")}},
		{"Decision making", "How far ahead a rescue is judged, and how much your input counts.", nullptr, true, false, {S(PlanHorizon, "Plan a rescue over", 2, 60, " ticks"), S(Follow, "Follow a rescue before re-planning for", 0, 40, " ticks"), S(Deviation, "Respect for your input", 0, 5000, ""), S(Continuity, "Penalty for changing its decision", 0, 2000, ""), S(Settle, "Penalty for ending a rescue at speed", 0, 2000, ""), S(Travel, "Penalty for being saved elsewhere", 0, 5000, ""), S(Coast, "Coast on after a rescue for", 0, 60, " ticks"), S(HazardSpeed, "Full-speed penalty near danger at", 0, 2000, ""), B(HandBack, "Reward returning control to you")}},
		{"Fight harder in crowds", "Searches deeper and further for an escape when surrounded.", &g_Config.m_NullGodmodeDeep, true, false, {S(DeepHorizon, "Simulate a rescue over", 10, 60, " ticks"), S(DeepChain, "Search one move deeper from the best option", 0, 12, "")}},
		{"Action costs", "Relative cost of each move; a costly one can still be chosen.", nullptr, true, false, {S(Commitment, "Prefer the smallest action", 0, 20000, ""), S(HookCost, "Prefer moving over hooking", 0, 20000, ""), S(TeeHookCost, "Avoid hooking players", 0, 20000, ""), S(HookAge, "Penalty for releasing a held hook", 0, 2000, ""), S(AirJump, "Penalty for using your air jump", 0, 20000, ""), S(WallCost, "Avoid ending up against a wall", 0, 2000, ""), S(Chain, "Search deeper when stuck, from", 0, 12, "")}},
		{"Dodging shots", "Airborne shots are simulated, so dodging is precise.", &g_Config.m_NullGodmodeWeapons, false, false, {S(WeaponRadius, "Watch for shots within", 0, 6000, "")}},
		{"Dummy Avoid Freeze", "Runs survival on the tee you are not controlling, using your settings.", &g_Config.m_NullGodmodeDummy, false, false, {}},
		{"Weapon defense", "Uses your own weapons to get you out of trouble.", nullptr, false, true, {B(Grenade, "Fire your grenade to save yourself"), B(Hammer, "Hammer back at point-blank range"), B(AutoWeapon, "Switch to a better weapon to survive"), B(RestoreWeapon, "Switch back to your weapon afterwards")}},
		{"Avoid hammers", "Stays out of people's hammer range.", &g_Config.m_NullGodmodeHammerDodge, false, false, {{&g_Config.m_NullGodmodeHammerDodgeMode, "", 0, 1, "", 1}, S(HammerLookahead, "Detect it from", 0, 60, " ticks"), S(HammerCost, "How strongly it avoids that range", 0, 20000, ""), B(HammerRing, "Show the range")}},
		{"Position hold", "Stops someone from moving you against your will.", &g_Config.m_NullGodmodeKnockback, false, false, {{&g_Config.m_NullGodmodeKnockbackMode, "", 0, 1, "", 2}, S(KnockbackGrip, "How strongly it pulls you back", 0, 2000, "%"), S(KnockbackHold, "Keep trying to return for", 1, 300, " ticks"), S(KnockbackClearance, "Keep the return path clear of freeze by", 0, 300, "")}},
		{"Hook counter", "When hooked, moves you above or below the puller so they can't fling you.", &g_Config.m_NullGodmodeHookCounter, false, false, {S(HookCounterGap, "Aim past them by", 16, 400, ""), S(HookCounterPull, "How strongly it aims for that spot", 0, 300, "%")}},
		{"Auto hammer", "With a hammer in hand, swing at whoever comes into its reach.", &g_Config.m_NullAutoHammer, false, false, {
			{&g_Config.m_NullAutoHammerRange, "", 0, 2, "", 4, {"Touch", "Near", "Max"}},
			{&g_Config.m_NullBindAutoHammer, "Hold key", 0, 2048, "", 0, {}, true}}},
		{"Auto grenade", "With a grenade in hand, blast back whoever hammers you.", &g_Config.m_NullAutoGrenade, false, false, {
			{&g_Config.m_NullAutoGrenadeRange, "Answer a hammerer within", 1, 40, " tiles"},
			{&g_Config.m_NullAutoGrenadeDelay, "Throw this long after the hit", 0, 50, " ticks"},
			{&g_Config.m_NullAutoGrenadeShock, "Count a hit at a knock of", 1, 40},
			{&g_Config.m_NullBindAutoGrenade, "Hold key", 0, 2048, "", 0, {}, true}}},
		{"Quick stop", "Let go of the movement keys and stop dead instead of sliding.", &g_Config.m_NullQuickStop, false, false, {
			{&g_Config.m_NullQuickStopGround, "Only on the ground, never in the air", 0, 1}}},
		{"Aimbot", "Aims at a target for you, without moving your crosshair.", &g_Config.m_NullAimbot, false, true, {
			{&g_Config.m_NullAimbotWeaponSel, "", 0, 5, "", 5, {"Hook", "Hammer", "Pistol", "Shotgun", "Grenade", "Laser"}},
			{apAimEnabled[AimWeapon], "Aim this weapon", 0, 1},
			{apAimFov[AimWeapon], "Field of view", 5, 360, "°"},
			{apAimPriority[AimWeapon], "", 0, 1, "", 4, {"Nearest to crosshair", "Nearest to you"}},
			{apAimKnock[AimWeapon], "Aim for maximum knockback", 0, 1},
			{&g_Config.m_NullAimbotKnockRange, "Only closer than", 40, 800},
			{nullptr, "On-screen range ring", 0, 1, "", 6},
			{apAimDraw[AimWeapon], "", 0, 2, "", 4, {"Don't show", "Always show", "Show only when held"}},
			{apAimAlpha[AimWeapon], "Ring opacity", 5, 100, "%"},
			{nullptr, "Shared by all weapons", 0, 1, "", 6},
			{&g_Config.m_NullAimbotSticky, "Stay on the current target", 0, 1},
			{&g_Config.m_NullAimbotStickyMargin, "Switch only if another is better by", 0, 90, "%"},
			{&g_Config.m_NullAimbotLineOfSight, "Ignore targets behind a wall", 0, 1},
			{&g_Config.m_NullAimbotLead, "Lead moving targets", 0, 1},
			{&g_Config.m_NullPingLead, "Ping compensation (ticks)", 0, 10},
			{&g_Config.m_NullAimbotArc, "Lob grenades over walls", 0, 1},
			{&g_Config.m_NullAimbotWallbounce, "Bounce lasers off walls", 0, 1},
			{&g_Config.m_NullAimbotWallbounceRays, "Wall bounce search angles", 24, 360},
			{&g_Config.m_NullAimbotIgnoreFriends, "Never target a friend", 0, 1}}, 2},
		{"Automation", "Small actions performed for you, on top of your own input.", nullptr, false, true, {
			{&g_Config.m_NullAutoFire, "Auto fire", 0, 1}, {&g_Config.m_NullAutoRehook, "Auto rehook", 0, 1},
			{&g_Config.m_NullAutoRehookTicks, "Keep aiming at the lost target for", 1, 60, " ticks"}}, 4},
		{"Protections", "What this client reports about itself to a server.", nullptr, false, true, {
			{&g_Config.m_NullRandomTimeoutSeed, "Randomize timeout code", 0, 1}}, 4, 1},
		{"Version spoofing", "What version this build reports to a server.", &g_Config.m_NullVersionSpoof, false, false, {
			{nullptr, "Report as", 0, 1, "", 0, {}, false, nullptr, g_Config.m_NullVersionStr, sizeof(g_Config.m_NullVersionStr)},
			{&g_Config.m_NullVersionNum, "Version number", 0, 100000}}, 4, 1},
		{"Fake aim", "Reports a different aim direction than the one you use.", &g_Config.m_NullFakeAim, false, false, {
			{&g_Config.m_NullFakeAimMode, "", 0, 5, "", 4, {"Mouse", "Robot", "Spin", "Random", "Reverse", "Nearest"}},
			{&g_Config.m_NullFakeAimSpin, "Turn speed", 10, 3600, "°/s"},
			{&g_Config.m_NullFakeAimSendAlways, "Send on every tick it moves", 0, 1},
			{&g_Config.m_NullFakeAimVisible, "Preview what others see", 0, 1}}, 4},
		{"Other", "Miscellaneous options that change what this client sends.", nullptr, false, true, {
			{&g_Config.m_NullKillOnFreeze, "Kill on freeze", 0, 1}, {&g_Config.m_NullKillOnDeepFreeze, "Kill on deep freeze", 0, 1},
			{&g_Config.m_NullHideChatBubble, "Hide chat bubble", 0, 1}, {&g_Config.m_NullPlayInMenu, "Keep playing while the menu is open", 0, 1},
			{&g_Config.m_NullMenuBackdrop, "Null backdrop behind the menus", 0, 1}}, 4, 1},
		{"Profiles", "Save all settings under a name; the file is yours to share.", nullptr, false, true, {}, 4, 1},
		{"Status HUD", "A corner panel showing which Null features are active.", &g_Config.m_NullHud, false, false, {
			{&g_Config.m_NullHudSize, "Size", 30, 250, "%"},
			{&g_Config.m_NullHudPos, "", 0, 5, "", 4, {"Top left", "Top right", "Bottom left", "Bottom right", "Middle left", "Middle right"}},
			{&g_Config.m_NullHudShowOff, "", 0, 1, "", 4, {"Only active", "All, with marks"}}}, 3},
		{"Aim lines", "Shows where a shot would go before it is fired.", &g_Config.m_NullAimLines, false, false, {
			{&g_Config.m_NullAimLineSelfOnly, "Only my own tee", 0, 1}, {&g_Config.m_NullAimLineGun, "Pistol", 0, 1},
			{&g_Config.m_NullAimLineShotgun, "Shotgun", 0, 1}, {&g_Config.m_NullAimLineGrenade, "Grenade", 0, 1}, {&g_Config.m_NullAimLineLaser, "Laser", 0, 1},
			{&g_Config.m_NullAimLineAlpha, "Opacity", 5, 100, "%"}, {&g_Config.m_NullAimLineTicks, "Trace a fired shot for", 5, 200, " ticks"}}, 3},
		{"Projectile lines", "Traces projectiles already in the air.", &g_Config.m_NullGodmodeShotPath, false, false, {
			{&g_Config.m_NullBulletLineSelfOnly, "Only my own projectiles", 0, 1}, {&g_Config.m_NullBulletLineGun, "Projectile", 0, 1},
			{&g_Config.m_NullBulletLineShotgun, "Shotgun", 0, 1}, {&g_Config.m_NullBulletLineGrenade, "Grenade", 0, 1},
			{&g_Config.m_NullBulletLineAlpha, "Opacity", 5, 100, "%"}, {&g_Config.m_NullGodmodeShotPathTicks, "Trace a projectile for", 5, 150, " ticks"}}, 3},
		{"Predicted path", "Shows where you are really heading, using the real physics.", &g_Config.m_NullPathliner, false, false, {
			{&g_Config.m_NullPathlinerTicks, "Prediction length", 5, 250, " ticks"}, {&g_Config.m_NullPathlinerWidth, "Line width", 1, 20},
			{&g_Config.m_NullPathlinerOpacity, "Opacity", 5, 100, "%"}, {&g_Config.m_NullPathlinerTaper, "Fade out toward the far end", 0, 1},
			{&g_Config.m_NullPathlinerGlow, "Glow", 0, 1}, {&g_Config.m_NullPathlinerFlow, "Moving light pulses", 0, 1},
			{&g_Config.m_NullPathlinerFlowSpeed, "Pulse speed", 10, 400}, {&g_Config.m_NullPathlinerStopOnFreeze, "Stop the path when frozen and still", 0, 1}}, 3, 1},
		{"Markers", "Key moments along the path worth highlighting.", nullptr, false, false, {
			{&g_Config.m_NullPathlinerFreezeMarker, "Freeze entry point", 0, 1}, {&g_Config.m_NullPathlinerDeathMarker, "Kill tile hit", 0, 1},
			{&g_Config.m_NullPathlinerEndMarker, "End of path", 0, 1}, {&g_Config.m_NullPathlinerLandingMarker, "Ground contact", 0, 1},
			{&g_Config.m_NullPathlinerHookMarker, "Hook attach point", 0, 1}, {&g_Config.m_NullPathlinerDummy, "Also show the dummy's path", 0, 1}}, 3, 1},
		{"Colors", "The line under the logo previews this palette.", nullptr, false, false, {
			{nullptr, "Path color near the tee", 0, 1, "", 0, {}, false, &g_Config.m_NullPathlinerColorNear},
			{nullptr, "Path color far ahead", 0, 1, "", 0, {}, false, &g_Config.m_NullPathlinerColorFar},
			{nullptr, "Path color while frozen", 0, 1, "", 0, {}, false, &g_Config.m_NullPathlinerColorFreeze},
			{nullptr, "Kill marker color", 0, 1, "", 0, {}, false, &g_Config.m_NullPathlinerColorDeath}}, 3, 1},
		{"Calculation depth", "The base variables behind every calculation. Raising these can cause serious FPS drops.", nullptr, false, false, {
			S(SimRadius, "Simulated surroundings", 0, 4000, ""), S(PlayerRadius, "But watch players within", 0, 8000, ""),
			S(Adversarial, "Assume other players are attacking", 0, 2, ""), S(HookAngles, "Surface scan resolution", 8, 128, "")}, 6},
		{"Debug overlay", "Shows what Avoid Freeze and the aimbot are calculating and thinking, live. For developers.", &g_Config.m_NullGodmodeDebug, false, false, {
			B(Trace, "Write the last 100 ticks to a file on death")}, 6},
		{"Ring around the tee", "A ring around your tee while god mode is acting. Turn it off for a cleaner view.", nullptr, false, true, {
			B(Indicator, "Show the ring while it acts")}, 6, 1},
		{"Fight replay", "Records both tees, the projectiles and a snapshot of the tiles.", nullptr, false, true, {
			{&g_Config.m_NullAutopsy, "", 0, 2, "", 4, {"Off", "When you finish", "And when you finish someone off"}}}, 6, 1},
		{"Serial killer", "Takes control to hunt. Never die keeps you alive while it works.", &g_Config.m_NullSerialKiller, false, false, {
			{&g_Config.m_NullSerialPick, "", 0, 3, "", 4, {"At crosshair", "Nearest", "War list", "Attack list"}},
			{&g_Config.m_NullSerialAdvance, "Move on to the next target", 0, 1},
			{&g_Config.m_NullSerialRange, "Chase targets within", 100, 4000},
			{&g_Config.m_NullSerialLead, "Lead the target by", 0, 40, " ticks"},
			{&g_Config.m_NullSerialGiveup, "Give up after being stuck for", 0, 500, " ticks"},
			{&g_Config.m_NullSerialHook, "Hold them with the hook", 0, 1},
			{&g_Config.m_NullSerialHammer, "Hammer at point-blank range", 0, 1},
			{&g_Config.m_NullSerialGrenade, "Use the grenade in between", 0, 1},
			{&g_Config.m_NullSerialWakeGuard, "Never hammer a frozen player", 0, 1},
			{&g_Config.m_NullSerialShove, "Push them toward the freeze", 0, 1},
			{&g_Config.m_NullSerialShoveRange, "Look for freeze around the target within", 0, 1000},
			{&g_Config.m_NullSerialHaul, "Prefer dragging over hitting by", 0, 1000},
			{&g_Config.m_NullSerialCarry, "Carry a frozen player up to", 0, 3000},
			{&g_Config.m_NullSerialSafety, "Value its own footing by", 0, 1000},
			{&g_Config.m_NullSerialSafeRange, "Look for hazards around its footing within", 0, 1000},
			{&g_Config.m_NullSerialPress, "Value being right on top of them by", 0, 1000},
			{&g_Config.m_NullSerialForesight, "Value their possible retaliation by", 0, 2000},
			{&g_Config.m_NullSerialForesightRange, "Count targets within", 0, 2000},
			{&g_Config.m_NullSerialIgnoreFriends, "Never target a friend", 0, 1},
			{&g_Config.m_NullSerialDraw, "Highlight the target", 0, 1},
			{&g_Config.m_NullSerialPathInterval, "Recalculate the route every", 1, 200, " ticks"}}, 1},
		{"Serial killer on the dummy", "Hunts on the tee you are not controlling, with its own target choice.", &g_Config.m_NullSerialKillerDummy, false, false, {
			{&g_Config.m_NullSerialPickDummy, "", 0, 3, "", 4, {"Nearest", "War list", "Attack list", "Same as main"}},
			{&g_Config.m_NullSerialOwn, "Let the dummy hunt your own tee", 0, 1}}, 1},
		{"Fly mode", "You steer a marker and the tee is held on it.", &g_Config.m_NullFlymode, false, false, {
			{&g_Config.m_NullFlymodeSpeed, "Marker speed", 50, 4000},
			{&g_Config.m_NullFlymodeHorizon, "Lookahead", 4, 80, " ticks"},
			{&g_Config.m_NullFlymodeSafetyTicks, "Check this far beyond each move", 0, 60, " ticks"},
			{&g_Config.m_NullFlymodePathNodes, "Route search effort (tiles)", 500, 2000000},
			{&g_Config.m_NullFlymodeProfile, "Report planning time to the console", 0, 1}}, 4},
		{"Pathfinding", "Routes around obstacles instead of into them.", &g_Config.m_NullFlymodePathfinder, false, false, {
			{&g_Config.m_NullFlymodePathDraw, "Show the route", 0, 1},
			{&g_Config.m_NullFlymodePathAvoidFreeze, "Keep the route out of freeze", 0, 1},
			{&g_Config.m_NullFlymodePathInterval, "Recalculate every", 1, 200, " ticks"}}, 4},
		{"How much of the map it scans", "Larger is more thorough but slower.", nullptr, false, true, {
			{&g_Config.m_NullFlymodeSimRadius, "Simulated surroundings", 0, 4000}}, 4},
		{"OpenClient", "Optional additions to NullClient's original survival behavior.", nullptr, false, true, {
			{&g_Config.m_NullPreserveJump, "Preserve a manual jump when a rescue supports it", 0, 1},
			{&g_Config.m_NullRocketRestoreWeapon, "Weapon return", 0, 2, "", 3}}, 4},
	};
#undef B
#undef S
	static bool s_aOpen[std::size(aCards)]{};
	static CButtonContainer s_aExpand[std::size(aCards)];
	static CButtonContainer s_aReset[std::size(aCards)];
	const auto ResetValue = [&](int *pValue) {
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc) \
	if(pValue == &g_Config.m_##Name) \
	{ \
		*pValue = Def; \
		return; \
	}
#define MACRO_CONFIG_COL(Name, Script, Def, Flags, Desc)
#define MACRO_CONFIG_STR(Name, Script, Size, Def, Flags, Desc)
#include <engine/shared/nullclient_config.h>
#include <engine/shared/nullclient_features_config.h>
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
#undef MACRO_CONFIG_INT
		if(pValue == &g_Config.m_NullPreserveJump)
			*pValue = 0;
		if(pValue == &g_Config.m_NullRocketRestoreWeapon)
		{
			*pValue = 0;
			g_Config.m_NullGodmodeRestoreWeapon = 0;
		}
	};
	const auto Icon = [&](CButtonContainer *pId, const char *pGlyph, const CUIRect &Rect, float Alpha) {
		const auto Color = TextRender()->GetTextColor();
		TextRender()->TextColor(1, 1, 1, Ui()->HotItem() == pId ? 1 : Alpha);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Ui()->DoLabel(&Rect, pGlyph, 12, TEXTALIGN_MC);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->TextColor(Color);
		return Ui()->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT);
	};
	static int s_Page = 0;
	static CButtonContainer s_aTabs[8];
	CUIRect Tabs, Tab;
	CUIRect Branding, ResetAll, Status;
	MainView.HSplitTop(34, &Branding, &MainView);
	Branding.VSplitRight(90, &Branding, &ResetAll);
	Ui()->DoLabel(&Branding, "NullClient 3.6", 24, TEXTALIGN_ML);
	static CButtonContainer s_ResetAll;
	if(DoButton_Menu(&s_ResetAll, Localize("Reset all"), 0, &ResetAll))
		PopupConfirm(Localize("Reset Null settings"), Localize("Reset every Null setting to its default value?"), Localize("Reset"), Localize("Cancel"), &CMenus::ResetSettingsNullClient);
	MainView.HSplitTop(18, &Status, &MainView);
	char aStatus[192], aPath[48];
	if(g_Config.m_NullPathliner) str_format(aPath, sizeof(aPath), Localize("path: %d ticks"), g_Config.m_NullPathlinerTicks);
	else str_copy(aPath, Localize("path: off"));
	str_format(aStatus, sizeof(aStatus), "%s   ·   %s   ·   %s", Localize(g_Config.m_NullGodmode ? "god mode: on" : "god mode: off"), Localize(g_Config.m_NullFlymode ? "fly mode: on" : "fly mode: off"), aPath);
	const auto StatusColor = TextRender()->GetTextColor(); TextRender()->TextColor(1, 1, 1, .5f);
	Ui()->DoLabel(&Status, aStatus, 11, TEXTALIGN_ML); TextRender()->TextColor(StatusColor);
	MainView.HSplitTop(6, nullptr, &MainView);
	MainView.HSplitTop(20, &Tabs, &MainView);
	const char *apPages[] = {"Survival", "Combat", "Aimbot", "Vision", "Misc", "Binds", "Diagnostics", "Warlist"};
	const float TabWidth = Tabs.w / 8;
	for(int i = 0; i < 8; ++i)
	{
		Tabs.VSplitLeft(TabWidth, &Tab, &Tabs);
		if(DoButton_MenuTab(&s_aTabs[i], Localize(apPages[i]), s_Page == i, &Tab, IGraphics::CORNER_ALL))
			s_Page = i;
	}
	MainView.HSplitTop(10, nullptr, &MainView);
	static CScrollRegion s_aScroll[8];
	auto &Scroll = s_aScroll[s_Page];
	CScrollRegionParams Params;
	Params.m_ScrollUnit = 120;
	Scroll.Begin(&MainView, &Params);
	MainView.VMargin(std::max(0.0f, (MainView.w - (s_Page == 0 ? 560.0f : 780.0f)) / 2), &MainView);
	CUIRect Left, Right;
	MainView.VSplitMid(&Left, &Right, 10);
	if(s_Page == 7)
	{
		auto &Null = GameClient()->m_NullClient;
		auto &Warlist = Null.Warlist();
		static char s_aGroup[64] = "war", s_aMember[64] = "";
		static CLineInput s_Group(s_aGroup,sizeof(s_aGroup)), s_Member(s_aMember,sizeof(s_aMember));
		static CButtonContainer s_Add;
		CUIRect Row, Label, Button;
		MainView.HSplitTop(24,&Row,&MainView); Row.VSplitLeft(100,&Label,&Row);
		Ui()->DoLabel(&Label,Localize("Group"),13,TEXTALIGN_ML); Ui()->DoEditBox(&s_Group,&Row,13);
		MainView.HSplitTop(4,nullptr,&MainView); MainView.HSplitTop(24,&Row,&MainView); Row.VSplitLeft(100,&Label,&Row);
		Ui()->DoLabel(&Label,Localize("Name"),13,TEXTALIGN_ML); Row.VSplitRight(90,&Row,&Button);
		Ui()->DoEditBox(&s_Member,&Row,13);
		if(DoButton_Menu(&s_Add,Localize("Add"),0,&Button) && Warlist.Edit(s_aGroup,s_aMember,true)) Null.SaveWarlist();
		static std::map<std::string,CButtonContainer> s_Remove;
		std::string RemoveGroup, RemoveName;
		for(const auto &Group : Warlist.m_vGroups)
		{
			MainView.HSplitTop(12,nullptr,&MainView); MainView.HSplitTop(24,&Row,&MainView);
			if(Scroll.AddRect(Row)) Ui()->DoLabel(&Row,Group.m_Name.c_str(),15,TEXTALIGN_ML);
			for(const auto &Name : Group.m_vMembers)
			{
				MainView.HSplitTop(23,&Row,&MainView); if(!Scroll.AddRect(Row)) continue;
				Row.VSplitRight(90,&Row,&Button); Ui()->DoLabel(&Row,Name.c_str(),13,TEXTALIGN_ML);
				if(DoButton_Menu(&s_Remove[Group.m_Name + "\n" + Name],Localize("Remove"),0,&Button)) { RemoveGroup = Group.m_Name; RemoveName = Name; }
			}
		}
		if(!RemoveName.empty() && Warlist.Edit(RemoveGroup,RemoveName,false)) Null.SaveWarlist();
	}
	if(s_Page == 5)
	{
		struct CBind { const char *m_pLabel; int *m_pKey; int m_Group; };
		const CBind aBinds[] = {
			{"Avoid Freeze", &g_Config.m_NullBindGodmode, 0}, {"Avoid Freeze (dummy)", &g_Config.m_NullBindGodmodeDummy, 0},
			{"Dodging shots", &g_Config.m_NullBindDodging, 0}, {"Avoid hammers", &g_Config.m_NullBindHammerDodge, 1},
			{"Auto hammer (hold)", &g_Config.m_NullBindAutoHammer, 1}, {"Auto grenade (hold)", &g_Config.m_NullBindAutoGrenade, 1},
			{"Serial Killer", &g_Config.m_NullBindSerial, 1}, {"Serial Killer (dummy)", &g_Config.m_NullBindSerialDummy, 1},
			{"Aimbot", &g_Config.m_NullBindAimbot, 2}, {"Aimbot hook", &g_Config.m_NullBindAimbotHook, 2},
			{"Balance bot (hold)", &g_Config.m_NullBindBalanceBot, 1}, {"Fly Mode", &g_Config.m_NullBindFlymode, 3},
			{"Fly Mode (hold)", &g_Config.m_NullBindFlyhold, 3}, {"Fake Aim", &g_Config.m_NullBindFakeAim, 4},
			{"Version Spoof", &g_Config.m_NullBindVersionSpoof, 4}};
		static CButtonContainer s_aBindKeys[15], s_aBindClear[15];
		const char *apGroups[] = {"Survival", "Combat", "Aimbot", "Vision", "Misc"};
		for(int Group = 0; Group < 5; ++Group)
		{
			auto &Column = Left.y <= Right.y ? Left : Right;
			CUIRect Header; Column.HSplitTop(26, &Header, &Column);
			if(Scroll.AddRect(Header)) Ui()->DoLabel(&Header, Localize(apGroups[Group]), 15, TEXTALIGN_ML);
			for(size_t i = 0; i < std::size(aBinds); ++i)
			{
				const auto &Bind = aBinds[i]; if(Bind.m_Group != Group) continue;
				CUIRect Row, Label; Column.HSplitTop(24, &Row, &Column); Column.HSplitTop(2, nullptr, &Column);
				if(!Scroll.AddRect(Row)) continue;
				Row.VSplitMid(&Label, &Row);
				Ui()->DoLabel(&Label, Localize(Bind.m_pLabel), 12, TEXTALIGN_ML);
				const CBindSlot Current(*Bind.m_pKey, 0);
				const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&s_aBindKeys[i], &s_aBindClear[i], &Row, Current, false, false);
				if(!Result.m_Aborted && Result.m_Bind != Current) *Bind.m_pKey = Result.m_Bind == EMPTY_BIND_SLOT ? 0 : Result.m_Bind.m_Key;
			}
			Column.HSplitTop(12, nullptr, &Column);
		}
	}
	std::vector<size_t> aOrder = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 14, 15, 10, 11, 12};
	for(size_t i = 16; i < std::size(aCards); ++i) aOrder.push_back(i);
	for(const size_t i : aOrder)
	{
		const auto &Card = aCards[i];
		const int Page = i < 9 ? 0 : i < 16 ? 1 : Card.m_Page;
		if(s_Page != Page) continue;
		if(Card.m_Child && (!g_Config.m_NullGodmode || !s_aOpen[0]))
		{
			s_aOpen[i] = false;
			continue;
		}
		if(Card.m_AlwaysOpen)
			s_aOpen[i] = true;
		const bool Open = s_aOpen[i];
		int Rows = Open ? Card.m_Settings.size() : 0;
		if(i == 22 && Open) Rows = 3 + std::min(static_cast<int>(GameClient()->m_NullClient.Profiles().size()), 12);
		if(i == 16 && Open && AimWeapon < 2) Rows -= 2;
		if(i == 9 && Open && !g_Config.m_NullGodmodeAutoWeapon)
			--Rows;
		CUIRect Panel, Body, Header, Title, Description, Toggle;
		auto &Column = (s_Page == 0 || s_Page == 2) ? MainView : (i < 16 ? (i >= 10 && i <= 12) : Card.m_Column == 1) ? Right : Left;
		Column.HSplitTop(63 + (Rows > 0 ? 10 + 22 * Rows : 0), &Panel, &Column);
		Column.HSplitTop(10, nullptr, &Column);
		if(Card.m_Child)
			Panel.VSplitLeft(20, nullptr, &Panel);
		if(!Scroll.AddRect(Panel))
			continue;
		Panel.Draw(ColorRGBA(1, 1, 1, Open ? .10f : .07f), IGraphics::CORNER_ALL, 8);
		Panel.Margin(10, &Body);
		Body.HSplitTop(43, &Header, &Body);
		Header.VSplitRight(Card.m_pEnabled ? 78 : 44, &Header, &Toggle);
		Header.HSplitTop(22, &Title, &Description);
		CUIRect Caret;
		Title.VSplitLeft(16, &Caret, &Title);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Ui()->DoLabel(&Caret, Open ? FontIcon::CHEVRON_UP : FontIcon::CHEVRON_DOWN, 9, TEXTALIGN_ML);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		if(i == 5 || i == 8 || i == 33 || i == 34)
		{
			CUIRect Badge;
			Title.VSplitRight(i == 5 ? 83 : 33, &Title, &Badge);
			Badge.HMargin(4, &Badge);
			Badge.Draw(ColorRGBA(1, .8f, .2f, .85f), IGraphics::CORNER_ALL, 2);
			const auto BeforeBadge = TextRender()->GetTextColor();
			TextRender()->TextColor(.08f, .05f, 0, 1);
			Ui()->DoLabel(&Badge, i == 5 ? "VERY HIGH CPU" : "BETA", 9, TEXTALIGN_MC);
			TextRender()->TextColor(BeforeBadge);
		}
		Ui()->DoLabel(&Title, Localize(Card.m_pTitle), 15, TEXTALIGN_ML);
		const auto TextColor = TextRender()->GetTextColor();
		TextRender()->TextColor(1, 1, 1, .45f);
		CTextCursor DescriptionCursor;
		DescriptionCursor.m_StartX = DescriptionCursor.m_X = Description.x;
		DescriptionCursor.m_StartY = DescriptionCursor.m_Y = Description.y;
		DescriptionCursor.m_FontSize = 11;
		DescriptionCursor.m_LineWidth = Description.w;
		DescriptionCursor.m_MaxLines = 2;
		TextRender()->TextEx(&DescriptionCursor, Localize(Card.m_pDescription), -1);
		TextRender()->TextColor(TextColor);
		if(Card.m_pEnabled)
		{
			CUIRect Switch;
			Toggle.VSplitLeft(34, &Switch, &Toggle);
			Switch.HMargin(15, &Switch);
			const float On = *Card.m_pEnabled ? 1.0f : 0.0f;
			const ColorRGBA Near = color_cast<ColorRGBA>(ColorHSLA(0x7fff96));
			const ColorRGBA Far = color_cast<ColorRGBA>(ColorHSLA(0xcdffa0));
			const ColorRGBA Accent(mix(Near.r, Far.r, .45f), mix(Near.g, Far.g, .45f), mix(Near.b, Far.b, .45f));
			Switch.Draw(ColorRGBA(mix(1.0f, Accent.r, On), mix(1.0f, Accent.g, On), mix(1.0f, Accent.b, On), .13f + .72f * On), IGraphics::CORNER_ALL, Switch.h / 2);
			CUIRect Knob{Switch.x + 2 + (Switch.w - Switch.h) * On, Switch.y + 2, Switch.h - 4, Switch.h - 4};
			Knob.Draw(ColorRGBA(1, 1, 1, .45f + .55f * On), IGraphics::CORNER_ALL, Knob.h / 2);
			if(Ui()->DoButtonLogic(Card.m_pEnabled, *Card.m_pEnabled, &Switch, BUTTONFLAG_LEFT))
				*Card.m_pEnabled ^= 1;
		}
		CUIRect Reset;
		Toggle.VSplitLeft(22, &Reset, &Toggle);
		if(Icon(&s_aReset[i], FontIcon::ARROW_ROTATE_LEFT, Reset, .45f))
		{
			if(Card.m_pEnabled)
				ResetValue(Card.m_pEnabled);
			for(const auto &Setting : Card.m_Settings)
			{
				if(Setting.m_pValue) ResetValue(Setting.m_pValue);
#define MACRO_CONFIG_INT(Name, Script, Def, Min, Max, Flags, Desc)
#define MACRO_CONFIG_COL(Name, Script, Def, Flags, Desc) if(Setting.m_pColor == &g_Config.m_##Name) *Setting.m_pColor = Def;
#define MACRO_CONFIG_STR(Name, Script, Size, Def, Flags, Desc) if(Setting.m_pText == g_Config.m_##Name) str_copy(Setting.m_pText, Def, Setting.m_TextSize);
#include <engine/shared/nullclient_features_config.h>
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
			}
			if(i == 16)
			{
#define MACRO_CONFIG_INT(Name, Script, Def, Min, Max, Flags, Desc) if(str_startswith(#Script, "null_aimbot_") || str_comp(#Script, "null_ping_lead") == 0) g_Config.m_##Name = Def;
#define MACRO_CONFIG_COL(Name, Script, Def, Flags, Desc)
#define MACRO_CONFIG_STR(Name, Script, Size, Def, Flags, Desc)
#include <engine/shared/nullclient_features_config.h>
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
			}
		}
		Ui()->DoToolTip(&s_aReset[i], &Reset, Localize("Reset this group to its defaults"));
		if(!Card.m_AlwaysOpen)
		{
			const bool Expand = Icon(&s_aExpand[i], FontIcon::GEAR, Toggle, Open ? .95f : .5f);
			Ui()->DoToolTip(&s_aExpand[i], &Toggle, Localize(Open ? "Collapse this group" : "Expand this group"));
			CUIRect Click = Panel;
			Click.h = 63;
			Click.w -= Card.m_pEnabled ? 78 : 44;
			if(Ui()->DoButtonLogic(&s_aOpen[i], Open, &Click, BUTTONFLAG_LEFT) ||
				Expand)
				s_aOpen[i] = !Open;
		}
		if(!Open)
			continue;
		Body.HSplitTop(10, nullptr, &Body);
		const bool WasEnabled = Ui()->Enabled();
		Ui()->SetEnabled(WasEnabled && (!Card.m_pEnabled || *Card.m_pEnabled));
		if(i == 22)
		{
			static char s_aProfileName[64] = "";
			static CLineInput s_Name(s_aProfileName, sizeof(s_aProfileName));
			static CButtonContainer s_aActions[4];
			static std::string s_Result;
			CUIRect Row;
			Body.HSplitTop(20, &Row, &Body);
			s_Name.SetEmptyText(Localize("my settings"));
			Ui()->DoClearableEditBox(&s_Name, &Row, 13);
			Body.HSplitTop(2, nullptr, &Body);
			Body.HSplitTop(20, &Row, &Body);
			const char *apActions[] = {"Save", "Load", "Delete", "Folder"};
			const float Width = Row.w / 4;
			for(int Action = 0; Action < 4; ++Action)
			{
				CUIRect Button;
				Row.VSplitLeft(Width, &Button, &Row);
				if(DoButton_Menu(&s_aActions[Action], Localize(apActions[Action]), 0, &Button))
				{
					auto &Null = GameClient()->m_NullClient;
					if(Action == 0) s_Result = Localize(Null.SaveProfile(s_aProfileName) ? "Profile saved" : "Could not save profile");
					if(Action == 1) s_Result = Localize(Null.LoadProfile(s_aProfileName) ? "Profile loaded" : "Could not load profile");
					if(Action == 2) s_Result = Localize(Null.DeleteProfile(s_aProfileName) ? "Profile deleted" : "Could not delete profile");
					if(Action == 3) Null.OpenProfileFolder();
				}
			}
			const auto Names = GameClient()->m_NullClient.Profiles();
			static CButtonContainer s_aProfileButtons[12];
			for(size_t NameIndex = 0; NameIndex < std::min<size_t>(12, Names.size()); ++NameIndex)
			{
				const auto &Name = Names[NameIndex];
				Body.HSplitTop(2, nullptr, &Body);
				Body.HSplitTop(20, &Row, &Body);
				if(DoButton_Menu(&s_aProfileButtons[NameIndex], Name.c_str(), 0, &Row)) str_copy(s_aProfileName, Name.c_str());
			}
			if(!s_Result.empty())
			{
				Body.HSplitTop(2, nullptr, &Body);
				Body.HSplitTop(20, &Row, &Body);
				if(Scroll.AddRect(Row)) Ui()->DoLabel(&Row, s_Result.c_str(), 12, TEXTALIGN_ML);
			}
		}
		for(const auto &Setting : Card.m_Settings)
		{
			if(i == 16 && AimWeapon < 2 && ((Setting.m_pValue == nullptr && Setting.m_Choice == 0) || Setting.m_pValue == &g_Config.m_NullAimbotKnockRange)) continue;
			if(i == 9 && Setting.m_pValue == &g_Config.m_NullGodmodeRestoreWeapon && !g_Config.m_NullGodmodeAutoWeapon)
				continue;
			CUIRect Row;
			Body.HSplitTop(20, &Row, &Body);
			Body.HSplitTop(2, nullptr, &Body);
			if(!Scroll.AddRect(Row))
				continue;
			if(Setting.m_Choice == 6)
			{
				Ui()->DoLabel(&Row, Localize(Setting.m_pLabel), 12, TEXTALIGN_ML);
			}
			else if(Setting.m_pColor)
			{
				static CButtonContainer s_aColorReset[4];
				const size_t ColorIndex = &Setting - Card.m_Settings.begin();
				DoLine_ColorPicker(&s_aColorReset[ColorIndex], 20, 13, 0, &Row, Localize(Setting.m_pLabel), Setting.m_pColor, ColorRGBA(1, 1, 1, 1), false);
			}
			else if(Setting.m_pText)
			{
				static CLineInput s_VersionName;
				s_VersionName.SetBuffer(Setting.m_pText, Setting.m_TextSize);
				s_VersionName.SetEmptyText(Localize("DDNet, no build hash"));
				Ui()->DoClearableEditBox(&s_VersionName, &Row, 13);
			}
			else if(Setting.m_Choice == 5)
			{
				static CButtonContainer s_aWeapons[6];
				const float Width = Row.w / 6;
				int Weapon = 0;
				for(const char *pName : Setting.m_Options)
				{
					CUIRect Tab;
					Row.VSplitLeft(Width, &Tab, &Row);
					if(DoButton_MenuTab(&s_aWeapons[Weapon], Localize(pName), *Setting.m_pValue == Weapon, &Tab, IGraphics::CORNER_ALL))
						*Setting.m_pValue = Weapon;
					++Weapon;
				}
			}
			else if(Setting.m_Key)
			{
				static CButtonContainer s_aKeys[2], s_aClear[2];
				const int KeyIndex = Setting.m_pValue == &g_Config.m_NullBindAutoGrenade;
				CUIRect Label;
				Row.VSplitLeft(90, &Label, &Row);
				Ui()->DoLabel(&Label, Localize(Setting.m_pLabel), 13, TEXTALIGN_ML);
				const CBindSlot Current(*Setting.m_pValue, 0);
				const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&s_aKeys[KeyIndex], &s_aClear[KeyIndex], &Row, Current, false, false);
				if(!Result.m_Aborted && Result.m_Bind != Current)
					*Setting.m_pValue = Result.m_Bind == EMPTY_BIND_SLOT ? 0 : Result.m_Bind.m_Key;
			}
			else if(Setting.m_Choice == 4)
			{
				struct CChoiceState { CUi::SDropDownState m_Choice; CScrollRegion m_Scroll; };
				static std::map<int *, CChoiceState> s_Choices;
				auto &Choice = s_Choices[Setting.m_pValue];
				Choice.m_Choice.m_SelectionPopupContext.m_pScrollRegion = &Choice.m_Scroll;
				std::vector<const char *> vNames;
				for(const char *pName : Setting.m_Options)
					vNames.push_back(Localize(pName));
				*Setting.m_pValue = Ui()->DoDropDown(&Row, *Setting.m_pValue, vNames.data(), vNames.size(), Choice.m_Choice);
			}
			else if(Setting.m_Choice == 3)
			{
				static CUi::SDropDownState s_ReturnMode;
				static CScrollRegion s_ReturnModeScroll;
				s_ReturnMode.m_SelectionPopupContext.m_pScrollRegion = &s_ReturnModeScroll;
				const char *apModes[] = {Localize("Do not switch back"), Localize("Switch back: NullClient"), Localize("Switch back: OpenKRX (confirmed shot)")};
				const int Current = g_Config.m_NullRocketRestoreWeapon ? 2 : g_Config.m_NullGodmodeRestoreWeapon ? 1 :
																   0;
				const int Selected = Ui()->DoDropDown(&Row, Current, apModes, 3, s_ReturnMode);
				if(Selected != Current)
				{
					g_Config.m_NullRocketRestoreWeapon = Selected == 2;
					g_Config.m_NullGodmodeRestoreWeapon = Selected == 1;
				}
				Ui()->DoToolTip(Setting.m_pValue, &Row, Localize("NullClient uses its original weapon return. OpenKRX waits for a confirmed grenade shot or a safe cancellation."));
			}
			else if(Setting.m_Choice)
			{
				static CUi::SDropDownState s_aChoice[2];
				static CScrollRegion s_aChoiceScroll[2];
				auto &Choice = s_aChoice[Setting.m_Choice - 1];
				Choice.m_SelectionPopupContext.m_pScrollRegion = &s_aChoiceScroll[Setting.m_Choice - 1];
				const char *apHammer[] = {Localize("Only whoever is hooking you"), Localize("Anyone holding a hammer")};
				const char *apKnockback[] = {Localize("Only when idle"), Localize("Also during a fight")};
				*Setting.m_pValue = Ui()->DoDropDown(&Row, *Setting.m_pValue, Setting.m_Choice == 1 ? apHammer : apKnockback, 2, Choice);
			}
			else if(Setting.m_Min == 0 && Setting.m_Max == 1)
			{
				if(i == 9 && Setting.m_pValue == &g_Config.m_NullGodmodeRestoreWeapon)
					Row.VSplitLeft(20, nullptr, &Row);
				if(DoButton_CheckBox(Setting.m_pValue, Localize(Setting.m_pLabel), *Setting.m_pValue, &Row))
					*Setting.m_pValue ^= 1;
			}
			else
				Ui()->DoScrollbarOption(Setting.m_pValue, Setting.m_pValue, &Row, Localize(Setting.m_pLabel), Setting.m_Min, Setting.m_Max, &CUi::ms_LinearScrollbarScale, 0, Localize(Setting.m_pSuffix));
		}
		Ui()->SetEnabled(WasEnabled);
	}
	Scroll.End();
}


void CMenus::ResetSettingsNullClient()
{
	g_Config.m_NullPreserveJump = 0;
	g_Config.m_NullRocketRestoreWeapon = 0;
#define MACRO_CONFIG_INT(Name, Script, Def, Min, Max, Flags, Desc) g_Config.m_##Name = Def;
#define MACRO_CONFIG_COL(Name, Script, Def, Flags, Desc) g_Config.m_##Name = Def;
#define MACRO_CONFIG_STR(Name, Script, Size, Def, Flags, Desc) str_copy(g_Config.m_##Name, Def);
#include <engine/shared/nullclient_config.h>
#include <engine/shared/nullclient_features_config.h>
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
	GameClient()->m_NullClient.OnReset();
}
