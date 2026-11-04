#ifndef GAME_CLIENT_NULLCLIENT_NATIVE_RETURN_H
#define GAME_CLIENT_NULLCLIENT_NATIVE_RETURN_H

#include "output.h"

namespace OpenKrx::NullClient
{
	// The host controls persist automatic WantedWeapon writes in their live
	// input slot. Do not misclassify that echo as a new manual selection and
	// cancel Null's original m_RestoreWeapon on the very next Apply call.
	// All return timing and state transitions still belong to COutputState.
	class CNativeReturnFeedback
	{
		int m_LastWanted = -1;
		bool m_Automatic = false;

	public:
		void Reset() { *this = {}; }
		CNetObj_PlayerInput Prepare(CNetObj_PlayerInput Input, const COutputState &State, const CSettings &Settings) const
		{
			if(Settings.m_AutoWeapon && Settings.m_RestoreWeapon && State.m_RestoreWeapon >= 0 &&
				m_Automatic && Input.m_WantedWeapon == m_LastWanted)
				Input.m_WantedWeapon = State.m_RestoreWeapon + 1;
			return Input;
		}
		void Observe(const CNetObj_PlayerInput &Input, const CNetObj_PlayerInput &Output, bool Enabled)
		{
			m_LastWanted = Output.m_WantedWeapon;
			m_Automatic = Enabled && Output.m_WantedWeapon != Input.m_WantedWeapon;
		}
	};
}
#endif
