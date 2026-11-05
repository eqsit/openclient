#include "warlist.h"

#include <base/io.h>

#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <algorithm>
#include <cstdio>

namespace OpenKrx::NullClient
{
	int CWarlist::Find(const std::string &Name) const
	{
		for(size_t i = 0; i < m_vGroups.size(); ++i)
			if(m_vGroups[i].m_Name == Name)
				return i;
		return -1;
	}
	bool CWarlist::Contains(int Group, const char *pName) const
	{
		return Group >= 0 && Group < (int)m_vGroups.size() &&
		       std::find(m_vGroups[Group].m_vMembers.begin(), m_vGroups[Group].m_vMembers.end(), pName) != m_vGroups[Group].m_vMembers.end();
	}
	bool CWarlist::Edit(const std::string &Group, const std::string &Name, bool Add)
	{
		if(Group.empty() || Name.empty() || Group.size() > 63 || Name.size() > 63 || Group.find_first_of("\r\n") != std::string::npos || Name.find_first_of("\r\n") != std::string::npos)
			return false;
		int Index = Find(Group);
		if(Index < 0 && Add && m_vGroups.size() < 128)
		{
			m_vGroups.push_back({Group});
			Index = m_vGroups.size() - 1;
		}
		if(Index < 0)
			return false;
		auto &Members = m_vGroups[Index].m_vMembers;
		auto It = std::find(Members.begin(), Members.end(), Name);
		if(Add && It == Members.end() && Members.size() < 4096)
			Members.push_back(Name);
		else if(!Add && It != Members.end())
			Members.erase(It);
		else
			return false;
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
				unsigned Color = 0xff8080;
				int Enabled = 1, Group = -1;
				while(const char *p = Reader.Get())
				{
					if(p[0] == 'c' && p[1] == ' ')
						std::sscanf(p + 2, "%u %d", &Color, &Enabled);
					else if(p[0] == 'n' && p[1] == ' ' && p[2] && m_vGroups.size() < 128)
					{
						m_vGroups.push_back({std::string(p + 2).substr(0, 63), Color, Enabled != 0, {}});
						Group = m_vGroups.size() - 1;
					}
					else if(p[0] == 'm' && p[1] == ' ' && p[2] && Group >= 0 && m_vGroups[Group].m_vMembers.size() < 4096)
						Edit(m_vGroups[Group].m_Name, std::string(p + 2).substr(0, 63), true);
				}
			}
		}
		if(Find("war") < 0)
			m_vGroups.push_back({"war"});
		if(Find("attack") < 0)
			m_vGroups.push_back({"attack"});
	}
	bool CWarlist::Save(IStorage *pStorage) const
	{
		// Original 2599d0 records c/n/m, with exact case-sensitive member names.
		std::string Text = "# Null warlist\n";
		for(const auto &Group : m_vGroups)
		{
			Text += "c " + std::to_string(Group.m_Color) + " " + std::to_string(Group.m_Enabled) + "\n";
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
