#include "component.h"
#include <base/str.h>
#include <base/io.h>
#include <engine/client.h>
#include <game/client/gameclient.h>
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

std::vector<std::string> CNullClient::Profiles()
{
	std::vector<std::string> Files;
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
	IOHANDLE File = Storage()->OpenFile(Path.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File) return false;
	const char *pHeader = "# Null settings profile.\n# Loading applies only null_ options; every other line is ignored.\n";
	bool Ok = io_write(File, pHeader, str_length(pHeader)) == static_cast<unsigned>(str_length(pHeader));
	const auto Write = [&](const char *pKey, const char *pValue) {
		char aLine[1024];
		str_format(aLine, sizeof(aLine), "%s %s\n", pKey, pValue);
		Ok &= io_write(File, aLine, str_length(aLine)) == static_cast<unsigned>(str_length(aLine));
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
	while(const char *pLine = Reader.Get())
	{
		pLine = str_skip_whitespaces_const(pLine);
		const char *pEnd = str_skip_to_whitespace_const(pLine);
		const std::string Key(pLine, pEnd);
		bool Known = false;
#define MACRO_CONFIG_INT(Name, Script, Def, Min, Max, Flags, Desc) Known |= Key == #Script;
#define MACRO_CONFIG_COL(Name, Script, Def, Flags, Desc) Known |= Key == #Script;
#define MACRO_CONFIG_STR(Name, Script, Size, Def, Flags, Desc) Known |= Key == #Script;
#include <engine/shared/nullclient_config.h>
#include <engine/shared/nullclient_features_config.h>
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
		if(Known && SingleOption(pLine))
			Console()->ExecuteLine(pLine, IConsole::CLIENT_ID_UNSPECIFIED, false);
	}
	OnReset();
	return true;
}

bool CNullClient::DeleteProfile(const char *pName)
{
	const std::string Name = ProfileName(pName);
	if(Name.empty()) return false;
	return Storage()->RemoveFile(("null_profiles/" + Name + ".cfg").c_str(), IStorage::TYPE_SAVE);
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
		auto *pSelf = static_cast<CNullClient *>(pUser); pSelf->m_Reports.Clear(pSelf->Storage(), true);
	}, this, "Delete the saved fight reports");
	Console()->Register("null_trace_dump", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser); pSelf->m_Reports.Dump(pSelf->GameClient(),g_Config.m_ClDummy,false,"manual");
	}, this, "Save the last 100 recorded Avoid ticks");
	Console()->Register("null_trace_clear", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser); pSelf->m_Reports.Clear(pSelf->Storage(), false);
	}, this, "Delete the saved Avoid traces and clear the recorded history");
	Console()->Register("null_profile_save", "s[name]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/profiles", pSelf->SaveProfile(pResult->GetString(0)) ? "profile saved" : "could not save profile");
	}, this, "Save every Null setting as a named profile");
	Console()->Register("null_profile_load", "s[name]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/profiles", pSelf->LoadProfile(pResult->GetString(0)) ? "profile loaded" : "could not load profile");
	}, this, "Apply a named Null settings profile");
	Console()->Register("null_profile_delete", "s[name]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/profiles", pSelf->DeleteProfile(pResult->GetString(0)) ? "profile deleted" : "could not delete profile");
	}, this, "Delete a named Null settings profile");
	Console()->Register("null_profile_folder", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) { static_cast<CNullClient *>(pUser)->OpenProfileFolder(); }, this, "Open the folder that holds the Null settings profiles");
	Console()->Register("null_profile_list", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
		auto *pSelf = static_cast<CNullClient *>(pUser);
		for(const auto &Name : pSelf->Profiles()) pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "null/profiles", Name.c_str());
	}, this, "List the saved Null settings profiles");
}

bool CNullClient::HandleWarlistCommand(const char *pLine)
{
	if(!Selected()) return false;
	pLine = str_skip_whitespaces_const(pLine);
	struct CCommand { const char *m_pCommand; const char *m_pGroup; bool m_Add; };
	for(const auto &Command : {CCommand{"!war","war",true}, CCommand{"!unwar","war",false}, CCommand{"!attack","attack",true}, CCommand{"!unattack","attack",false}})
	{
		const char *pName = str_startswith(pLine,Command.m_pCommand);
		if(!pName || (*pName && *pName != ' ')) continue;
		pName = str_skip_whitespaces_const(pName);
		std::string Name(pName); while(!Name.empty() && Name.back() == ' ') Name.pop_back();
		const bool Changed = m_Warlist.Edit(Command.m_pGroup,Name,Command.m_Add);
		if(Changed && !SaveWarlist()) Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD,"null/warlist","could not save warlist");
		else Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD,"null/warlist",Name.empty() ? "Warlist: type a player name after the command" : Changed ? "warlist updated" : "warlist unchanged");
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
	};
	bool Consumed = false;
	for(const auto &Bind : aToggles)
		if(Bind.m_Key && Bind.m_Key == Event.m_Key)
			*Bind.m_pEnabled ^= 1, Consumed = true;
	return Consumed;
}
}
