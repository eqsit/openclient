#ifndef GAME_CLIENT_NULLCLIENT_WARLIST_H
#define GAME_CLIENT_NULLCLIENT_WARLIST_H
#include <string>
#include <vector>
class IStorage;
namespace OpenKrx::NullClient
{
	class CWarlist
	{
	public:
		struct CGroup
		{
			std::string m_Name;
			unsigned m_Color = 0xff8080;
			bool m_Enabled = true;
			std::vector<std::string> m_vMembers;
		};
		std::vector<CGroup> m_vGroups;
		void Load(IStorage *pStorage);
		bool Save(IStorage *pStorage) const;
		int Find(const std::string &Name) const;
		bool Contains(int Group, const char *pName) const;
		bool Edit(const std::string &Group, const std::string &Name, bool Add);
	};
}
#endif
