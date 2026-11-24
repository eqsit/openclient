#include "warlist.h"

#include <base/color.h>
#include <base/io.h>
#include <base/str.h>

#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <string_view>
#include <utility>

namespace OpenKrx::NullClient
{
	namespace
	{
		// 0x1402a27d0 creates a group only while fewer than 0x18 (24) exist.
		constexpr size_t WARLIST_MAX_GROUPS = 24;
		constexpr size_t WARLIST_GROUP_NAME = 31;
		constexpr size_t WARLIST_LEGACY_GROUP_NAME = 63;
		constexpr size_t WARLIST_MEMBER_NAME = 63;
		// 0x1402a32b0 starts the file with this line.
		constexpr const char *WARLIST_HEADER = "# Null warlist - who you are fighting, kept by name. Edited in game.\n";

		// A 'c' line packs the HSL form of the colour (h, s, l bytes) in the same
		// integer the original prints with "%u"; the group itself keeps RGBA.
		ColorRGBA ParseColor(unsigned Packed)
		{
			return color_cast<ColorRGBA>(ColorHSLA(((Packed >> 16) & 0xff) / 255.0f, ((Packed >> 8) & 0xff) / 255.0f, (Packed & 0xff) / 255.0f, 1.0f));
		}
		unsigned PackColor(const ColorRGBA &Color)
		{
			const ColorHSLA Hsl = color_cast<ColorHSLA>(Color);
			const auto Byte = [](float Value) { return (unsigned)(int)(Value * 255.0f + 0.5f); };
			return (Byte(Hsl.h) << 16) + (Byte(Hsl.s) << 8) + Byte(Hsl.l);
		}
		void RestoreDefaultGroup(std::vector<CWarlist::CGroup> &vGroups, const char *pName, const ColorRGBA &Color, size_t Position)
		{
			const auto Matches = [&](const auto &Group) { return str_comp_nocase(Group.m_Name.c_str(), pName) == 0; };
			auto It = std::find_if(vGroups.begin(), vGroups.end(), [&](const auto &Group) {
				return Group.m_Protected && Matches(Group);
			});
			if(It == vGroups.end())
				It = std::find_if(vGroups.begin(), vGroups.end(), Matches);
			CWarlist::CGroup Group;
			if(It != vGroups.end())
			{
				Group = std::move(*It);
				vGroups.erase(It);
			}
			else
				Group.m_Color = Color;
			Group.m_Name = pName;
			Group.m_Protected = true;
			vGroups.insert(vGroups.begin() + Position, std::move(Group));
		}
	}

	int CWarlist::Find(const std::string &Name) const
	{
		for(size_t i = 0; i < m_vGroups.size(); ++i)
			if(str_comp_nocase(m_vGroups[i].m_Name.c_str(), Name.c_str()) == 0)
				return i;
		return -1;
	}
	bool CWarlist::Contains(int Group, const char *pName) const
	{
		// The group protection flag only affects removal.
		return Group >= 0 && Group < (int)m_vGroups.size() &&
		       std::find(m_vGroups[Group].m_vMembers.begin(), m_vGroups[Group].m_vMembers.end(), pName) != m_vGroups[Group].m_vMembers.end();
	}
	bool CWarlist::AddGroup(const std::string &Name)
	{
		if(Name.find_first_of("\r\n") != std::string::npos || m_vGroups.size() >= WARLIST_MAX_GROUPS)
			return false;
		char aName[WARLIST_GROUP_NAME + 1];
		str_copy(aName, Name.c_str());
		str_clean_whitespaces(aName);
		if(!aName[0] || Find(aName) >= 0)
			return false;
		const float Hue = std::fmod(static_cast<float>(m_vGroups.size()) * 0.147f, 1.0f);
		m_vGroups.push_back({aName, color_cast<ColorRGBA>(ColorHSLA(Hue, 0.75f, 0.6f, 1.0f))});
		return true;
	}
	bool CWarlist::RemoveGroup(const std::string &Name)
	{
		const int Index = Find(Name);
		if(Index < 0 || m_vGroups[Index].m_Protected)
			return false;
		m_vGroups.erase(m_vGroups.begin() + Index);
		return true;
	}
	bool CWarlist::Edit(const std::string &Group, const std::string &Name, bool Add)
	{
		if(Group.empty() || Name.empty() || Group.size() > WARLIST_LEGACY_GROUP_NAME || Name.size() > WARLIST_MEMBER_NAME || Group.find_first_of("\r\n") != std::string::npos || Name.find_first_of("\r\n") != std::string::npos)
			return false;
		int Index = Find(Group);
		if(Index < 0 && Add && AddGroup(Group))
			Index = m_vGroups.size() - 1;
		if(Index < 0)
			return false;
		auto &Members = m_vGroups[Index].m_vMembers;
		auto It = std::find(Members.begin(), Members.end(), Name);
		if(Add)
		{
			if(It != Members.end())
				return false;
			Members.push_back(Name);
			return true;
		}
		if(It == Members.end())
			return false;
		Members.erase(It);
		return true;
	}
	void CWarlist::Load(IStorage *pStorage)
	{
		m_vGroups.clear();
		if(IOHANDLE File = pStorage->OpenFile("null_warlist.cfg", IOFLAG_READ, IStorage::TYPE_SAVE))
		{
			CLineReader Reader;
			if(Reader.OpenFile(File))
			{
				bool LegacyRgb = false;
				CGroup *pGroup = nullptr;
				while(const char *p = Reader.Get())
				{
					if(std::string_view(p) == "# Null warlist")
						LegacyRgb = true;
					if(p[0] == '\0' || p[0] == '#')
						continue;
					if(p[0] == 'c' && p[1] == ' ')
					{
						unsigned Color = 0;
						int Protected = 0;
						std::sscanf(p + 2, "%u %d", &Color, &Protected);
						// Keep groups from older OpenNull files beyond the creation limit.
						pGroup = &m_vGroups.emplace_back();
						pGroup->m_Color = LegacyRgb ? ColorRGBA(Color) : ParseColor(Color);
						pGroup->m_Protected = Protected != 0;
					}
					else if(p[0] == 'n' && p[1] == ' ' && pGroup)
					{
						// Earlier OpenNull files support long persisted group names.
						pGroup->m_Name = p + 2;
					}
					else if(p[0] == 'm' && p[1] == ' ' && p[2] && pGroup)
					{
						auto &Members = pGroup->m_vMembers;
						if(std::find(Members.begin(), Members.end(), p + 2) == Members.end())
							Members.emplace_back(p + 2);
					}
				}
			}
		}
		RestoreDefaultGroup(m_vGroups, "war", ColorRGBA(0.9f, 0.22f, 0.22f, 1.0f), 0);
		RestoreDefaultGroup(m_vGroups, "attack", ColorRGBA(0.95f, 0.55f, 0.15f, 1.0f), 1);
	}
	bool CWarlist::Save(IStorage *pStorage) const
	{
		// Original 0x1402a32b0 records c/n/m, with exact case-sensitive member names.
		std::string Text = WARLIST_HEADER;
		for(const auto &Group : m_vGroups)
		{
			Text += "c " + std::to_string(PackColor(Group.m_Color)) + " " + std::to_string(Group.m_Protected ? 1 : 0) + "\n";
			Text += "n " + Group.m_Name + "\n";
			for(const auto &Name : Group.m_vMembers)
				Text += "m " + Name + "\n";
		}
		IOHANDLE File = pStorage->OpenFile("null_warlist.cfg.tmp", IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		bool Ok = io_write(File, Text.data(), Text.size()) == Text.size();
		Ok &= io_close(File) == 0;
		if(Ok)
			Ok = pStorage->RenameFile("null_warlist.cfg.tmp", "null_warlist.cfg", IStorage::TYPE_SAVE);
		if(!Ok)
			pStorage->RemoveFile("null_warlist.cfg.tmp", IStorage::TYPE_SAVE);
		return Ok;
	}
}
