#ifndef GAME_CLIENT_NULLCLIENT_WARLIST_H
#define GAME_CLIENT_NULLCLIENT_WARLIST_H
#include <base/color.h>

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
			// 0x1402a27d0 keeps the colour as RGBA and converts it to HSL on save.
			ColorRGBA m_Color = ColorRGBA(1.0f, 0.50196f, 0.50196f, 1.0f); // 0xff8080
			bool m_Protected = false;
			std::vector<std::string> m_vMembers;
		};
		std::vector<CGroup> m_vGroups;
		void Load(IStorage *pStorage);
		bool Save(IStorage *pStorage) const;
		int Find(const std::string &Name) const;
		bool Contains(int Group, const char *pName) const;
		bool AddGroup(const std::string &Name);
		bool RemoveGroup(const std::string &Name);
		bool Edit(const std::string &Group, const std::string &Name, bool Add);
	};
}
#endif
