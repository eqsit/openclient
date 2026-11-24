#include "component.h"
#include "settings.h"
#include <base/str.h>
#include <base/io.h>
#include <base/time.h>
#include <engine/client.h>
#include <game/client/gameclient.h>
#include <game/localization.h>
#include <engine/console.h>
#include <engine/storage.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <algorithm>
#include <string>

namespace OpenKrx::NullClient
{
namespace
{
// 1402508c0/140250480: ASCII alphanumerics, '-' and '_', internal spaces;
// at most 63 retained bytes, stripped leading/trailing spaces.
std::string ProfileName(const char *pName)
{
	std::string Name;
	for(; *pName && Name.size() < 63; ++pName)
	{
		const unsigned char Ch = *pName;
		if((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') ||
			(Ch >= '0' && Ch <= '9') || Ch == '-' || Ch == '_' || (Ch == ' ' && !Name.empty()))
			Name.push_back(Ch);
	}
	while(!Name.empty() && Name.back() == ' ')
		Name.pop_back();
	return Name;
}
bool SingleOption(const char *pLine)
{
	bool Quoted = false, Escaped = false;
	for(; *pLine; ++pLine)
	{
		if(Escaped) { Escaped = false; continue; }
		if(Quoted && *pLine == '\\') { Escaped = true; continue; }
		if(*pLine == '"') Quoted = !Quoted;
		if(!Quoted && *pLine == ';') return false;
	}
	return !Quoted;
}
}

const std::vector<std::string> &CNullClient::Profiles(bool Refresh)
{
	const auto Now = time_get_impl();
	if(!Refresh && !m_ProfilesDirty && Now < m_NextProfileScan)
		return m_vProfiles;
	m_NextProfileScan = Now + time_freq();
	m_ProfilesDirty = false;
	auto &Files = m_vProfiles;
	Files.clear();
	Storage()->ListDirectory(IStorage::TYPE_SAVE, "null_profiles", [](const char *pName, int IsDir, int, void *pUser) {
		if(!IsDir && str_endswith(pName, ".cfg"))
		{
			std::string Name(pName);
			Name.resize(Name.size() - 4);
			static_cast<std::vector<std::string> *>(pUser)->push_back(Name);
		}
		return 0;
	}, &Files);
	std::sort(Files.begin(), Files.end());
	return Files;
}

bool CNullClient::SaveProfile(const char *pName)
{
	const std::string Name = ProfileName(pName);
	if(Name.empty()) return false;
	Storage()->CreateFolder("null_profiles", IStorage::TYPE_SAVE);
	const std::string Path = "null_profiles/" + Name + ".cfg";
	const std::string Temporary = Path + ".tmp";
	IOHANDLE File = Storage()->OpenFile(Temporary.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File) return false;
	SyncAvoidModeSettings();
	const char *pHeader = "# Null settings profile, including OpenClient's Null extensions.\n# Loading applies only known Null options; every other line is ignored.\n";
	bool Ok = io_write(File, pHeader, str_length(pHeader)) == static_cast<unsigned>(str_length(pHeader));
	const auto Write = [&](const char *pKey, const char *pValue) {
		std::string Line(pKey);
		Line += ' ';
		Line += pValue;
		Line += '\n';
		Ok &= io_write(File, Line.data(), Line.size()) == Line.size();
	};
#define MACRO_CONFIG_INT(Name, Script, Def, Min, Max, Flags, Desc) { char aValue[32]; str_format(aValue, sizeof(aValue), "%d", g_Config.m_##Name); Write(#Script, aValue); }
#define MACRO_CONFIG_COL(Name, Script, Def, Flags, Desc) { char aValue[32]; str_format(aValue, sizeof(aValue), "%u", g_Config.m_##Name); Write(#Script, aValue); }
#define MACRO_CONFIG_STR(Name, Script, Size, Def, Flags, Desc) { char aValue[Size * 2 + 4]; aValue[0] = '"'; char *pAt = aValue + 1; str_escape(&pAt, g_Config.m_##Name, aValue + sizeof(aValue) - 2); *pAt++ = '"'; *pAt = 0; Write(#Script, aValue); }
#include <engine/shared/nullclient_config.h>
#include <engine/shared/nullclient_features_config.h>
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
	Ok &= io_close(File) == 0;
	if(Ok)
		Ok = Storage()->RenameFile(Temporary.c_str(), Path.c_str(), IStorage::TYPE_SAVE);
	if(!Ok)
		Storage()->RemoveFile(Temporary.c_str(), IStorage::TYPE_SAVE);
	m_ProfilesDirty |= Ok;
	return Ok;
}

bool CNullClient::LoadProfile(const char *pName)
{
	const std::string Name = ProfileName(pName);
	if(Name.empty()) return false;
	const std::string Path = "null_profiles/" + Name + ".cfg";
	IOHANDLE File = Storage()->OpenFile(Path.c_str(), IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File) return false;
	CLineReader Reader;
	if(!Reader.OpenFile(File)) return false;
	SyncAvoidModeSettings();
	bool HasFlatAvoidOption = false;
	while(const char *pLine = Reader.Get())
	{
		pLine = str_skip_whitespaces_const(pLine);
		const char *pEnd = str_skip_to_whitespace_const(pLine);
		const std::string Key(pLine, pEnd);
		if(!SingleOption(pLine))
			continue;
		// The 0x14029a460 allowlist is the only source of profile keys. The two
		// openkrx_* names are read for profiles written by older builds but are no
		// longer written, so the file stays loadable by the original (A9-007).
		bool Known = Key == "openkrx_null_preserve_jump" || Key == "openkrx_null_rocket_restore_weapon";
#define MACRO_CONFIG_INT(Name, Script, Def, Min, Max, Flags, Desc) \
	if(Key == #Script) \
	{ \
		Known = true; \
		HasFlatAvoidOption |= IsAvoidModeOption(&CConfig::m_##Name); \
	}
#define MACRO_CONFIG_COL(Name, Script, Def, Flags, Desc) Known |= Key == #Script;
#define MACRO_CONFIG_STR(Name, Script, Size, Def, Flags, Desc) Known |= Key == #Script;
#include <engine/shared/nullclient_config.h>
#include <engine/shared/nullclient_features_config.h>
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
		if(Known)
			Console()->ExecuteLine(pLine, IConsole::CLIENT_ID_UNSPECIFIED, false);
	}
	// Full OpenNull profiles preserve explicit live tuning. A bank-only profile
	// uses the same bank-change handling as an individual console edit.
	SyncAvoidModeSettings(HasFlatAvoidOption);
	OnReset();
	return true;
}

bool CNullClient::DeleteProfile(const char *pName)
{
	const std::string Name = ProfileName(pName);
	if(Name.empty()) return false;
	const bool Removed = Storage()->RemoveFile(("null_profiles/" + Name + ".cfg").c_str(), IStorage::TYPE_SAVE);
	m_ProfilesDirty |= Removed;
	return Removed;
}

void CNullClient::OpenProfileFolder()
{
	Storage()->CreateFolder("null_profiles", IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, "null_profiles", aPath, sizeof(aPath));
	Client()->ViewFile(aPath);
}

void CNullClient::OnConsoleInit()
{
	Console()->Register("null_serial_lock", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser); if(pSelf->Selected()) { pSelf->SynchronizeSlot(); pSelf->m_Serial.Lock(pSelf->GameClient(), pSelf->m_Warlist); }
	}, this, "Lock serial killer onto whoever is nearest the crosshair, or let the current one go");
	Console()->Register("null_warlist_add", "s[group] r[name]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser);
		if(pSelf->m_Warlist.Edit(pResult->GetString(0), pResult->GetString(1), true))
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/warlist", pSelf->SaveWarlist() ? "member added" : "could not save warlist");
	}, this, "Add an exact player name to a warlist group");
	Console()->Register("null_warlist_remove", "s[group] r[name]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser);
		if(pSelf->m_Warlist.Edit(pResult->GetString(0), pResult->GetString(1), false)) pSelf->SaveWarlist();
	}, this, "Remove an exact player name from a warlist group");
	Console()->Register("null_autopsy_now", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser); pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/autopsy", pSelf->m_Reports.Dump(pSelf->GameClient(),g_Config.m_ClDummy,true,"manual") ? "report saved" : "nothing recorded yet - switch null_autopsy on first");
	}, this, "Save the recorded fight now");
	Console()->Register("null_autopsy_clear", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser);
		const bool Removed = pSelf->m_Reports.Clear(pSelf->Storage(), true);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/autopsy", Removed ? "reports deleted" : "no reports to delete");
	}, this, "Delete the saved fight reports");
	Console()->Register("null_trace_dump", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser); pSelf->m_Reports.Dump(pSelf->GameClient(),g_Config.m_ClDummy,false,"manual");
	}, this, "Save the last 100 recorded Avoid ticks");
	const auto ClearTrace = [](IConsole::IResult *, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser);
		const bool Removed = pSelf->m_Reports.Clear(pSelf->Storage(), false);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/god", Removed ? "black box deleted" : "no black box to delete");
	};
	for(const char *pCommand : {"null_delete_blackbox", "null_trace_clear"})
		Console()->Register(pCommand, "", CFGFLAG_CLIENT, ClearTrace, this, "Delete the saved Avoid traces and clear the recorded history");
	Console()->Register("null_rescue_capture", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser);
		const bool Captured = pSelf->CaptureRescueScene();
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/rescue",
			Captured ? "Rescue simulation saved for this session." : "Enable rescue visuals and capture after a rescue calculation.");
	}, this, "Keep the latest rescue calculation for a demo scene");
	const auto PlayRescue = [](IConsole::IResult *, void *pUser) {
		static_cast<CNullClient *>(pUser)->StartRescueScene(true);
	};
	for(const char *pCommand : {"null_rescue_scene", "rescue_scene"})
		Console()->Register(pCommand, "", CFGFLAG_CLIENT, PlayRescue, this, "Play the captured rescue scene in a matching demo");
	Console()->Register("null_rescue_scene_stop", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
		static_cast<CNullClient *>(pUser)->StopRescueScene();
	}, this, "Stop the rescue scene and restore demo playback");
	Console()->Register("null_profile_save", "s[name]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser);
		const std::string Name = ProfileName(pResult->GetString(0));
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), pSelf->SaveProfile(Name.c_str()) ? "saved profile '%s'" : "could not save profile '%s'", Name.c_str());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/profiles", aBuf);
	}, this, "Save every Null setting as a named profile");
	Console()->Register("null_profile_load", "s[name]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser);
		const std::string Name = ProfileName(pResult->GetString(0));
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), pSelf->LoadProfile(Name.c_str()) ? "loaded profile '%s'" : "no profile '%s'", Name.c_str());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/profiles", aBuf);
	}, this, "Apply a named Null settings profile");
	Console()->Register("null_profile_delete", "s[name]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser);
		const std::string Name = ProfileName(pResult->GetString(0));
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), pSelf->DeleteProfile(Name.c_str()) ? "deleted profile '%s'" : "no profile '%s'", Name.c_str());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/profiles", aBuf);
	}, this, "Delete a named Null settings profile");
	Console()->Register("null_profile_folder", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) { static_cast<CNullClient *>(pUser)->OpenProfileFolder(); }, this, "Open the folder that holds the Null settings profiles");
	Console()->Register("null_profile_list", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser);
		const auto &vProfiles = pSelf->Profiles(true);
		if(vProfiles.empty())
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/profiles", "no profiles saved");
		for(const auto &Name : vProfiles) pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/profiles", Name.c_str());
	}, this, "List the saved Null settings profiles");
}

bool CNullClient::HandleWarlistCommand(const char *pLine)
{
	if(!Selected()) return false;
	while(*pLine == ' ')
		++pLine;
	struct CCommand { const char *m_pCommand, *m_pGroup, *m_pChanged, *m_pUnchanged, *m_pUsage; bool m_Add; };
	const CCommand aCommands[] = {
		{"!war", "war", "Warlist: '%s' added to war", "Warlist: '%s' is already at war", "Warlist: type a name, e.g. !war SomePlayer", true},
		{"!unwar", "war", "Warlist: '%s' removed from war", "Warlist: '%s' was not at war", "Warlist: type a name, e.g. !war SomePlayer", false},
		{"!attack", "attack", "Warlist: '%s' added to attack", "Warlist: '%s' is already on attack", "Warlist: type a name, e.g. !attack SomePlayer", true},
		{"!unattack", "attack", "Warlist: '%s' removed from attack", "Warlist: '%s' was not on attack", "Warlist: type a name, e.g. !attack SomePlayer", false}};
	for(const auto &Command : aCommands)
	{
		const char *pName = str_startswith(pLine, Command.m_pCommand);
		if(!pName || (*pName && *pName != ' '))
			continue;
		while(*pName == ' ')
			++pName;
		char aName[32];
		str_copy(aName, pName);
		str_clean_whitespaces(aName);
		if(!aName[0])
		{
			GameClient()->m_Chat.Echo(Localize(Command.m_pUsage));
			return true;
		}
		auto Previous = m_Warlist;
		const bool Changed = m_Warlist.Edit(Command.m_pGroup, aName, Command.m_Add);
		if(Changed && !SaveWarlist())
		{
			m_Warlist = std::move(Previous);
			GameClient()->m_Chat.Echo(Localize("Could not save the warlist"));
			return true;
		}
		char aMessage[256];
		str_format(aMessage, sizeof(aMessage), Localize(Changed ? Command.m_pChanged : Command.m_pUnchanged), aName);
		GameClient()->m_Chat.Echo(aMessage);
		return true;
	}
	return false;
}

bool CNullClient::OnInput(const IInput::CEvent &Event)
{
	// The input dispatcher forwards releases through active UI components.
	// Let Fly clear its latches before applying the UI/selected-profile gate.
	if(Event.m_Flags & IInput::FLAG_RELEASE)
		return m_Fly.OnInput(GameClient(), Event);
	if(!Selected() || GameClient()->m_GameConsole.IsActive() || GameClient()->m_Chat.IsActive() || GameClient()->m_Menus.IsActive() || GameClient()->m_KeyBinder.IsActive())
		return false;
	// Initialize before capturing the first held key, so OnRender cannot erase it.
	SynchronizeSlot();
	if(m_Fly.OnInput(GameClient(), Event)) return true;
	if(!(Event.m_Flags & IInput::FLAG_PRESS)) return false;
	struct CBind { int m_Key; int *m_pEnabled; };
	const CBind aToggles[] = {
		{g_Config.m_NullBindGodmode, &g_Config.m_NullGodmode},
		{g_Config.m_NullBindGodmodeDummy, &g_Config.m_NullGodmodeDummy},
		{g_Config.m_NullBindDodging, &g_Config.m_NullGodmodeWeapons},
		{g_Config.m_NullBindHammerDodge, &g_Config.m_NullGodmodeHammerDodge},
		{g_Config.m_NullBindRocketDodge, &g_Config.m_NullGodmodeRocketDodge},
		{g_Config.m_NullBindSerial, &g_Config.m_NullSerialKiller},
		{g_Config.m_NullBindSerialDummy, &g_Config.m_NullSerialKillerDummy},
		{g_Config.m_NullBindAimbot, &g_Config.m_NullAimbot},
		{g_Config.m_NullBindFakeAim, &g_Config.m_NullFakeAim},
		{g_Config.m_NullBindFlymode, &g_Config.m_NullFlymode},
		{g_Config.m_NullBindVersionSpoof, &g_Config.m_NullVersionSpoof},
		{g_Config.m_NullBindCounterHammer, &g_Config.m_NullCounterHammer},
		{g_Config.m_NullBindQuickStop, &g_Config.m_NullQuickStop},
		{g_Config.m_NullBindAutoHammerToggle, &g_Config.m_NullAutoHammer},
		{g_Config.m_NullBindAutoGrenadeToggle, &g_Config.m_NullAutoGrenade},
		{g_Config.m_NullBindAvoidRocket, &g_Config.m_NullGodmodeAvoidRocket},
		{g_Config.m_NullBindDummyLaserUnfreeze, &g_Config.m_NullDummyLaserUnfreeze},
		{g_Config.m_NullBindHookCounter, &g_Config.m_NullGodmodeHookCounter},
	};
	bool Consumed = false;
	for(const auto &Bind : aToggles)
		if(Bind.m_Key && Bind.m_Key == Event.m_Key)
			*Bind.m_pEnabled ^= 1, Consumed = true;
	return Consumed;
}
}
