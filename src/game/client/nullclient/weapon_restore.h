#ifndef GAME_CLIENT_NULLCLIENT_WEAPON_RESTORE_H
#define GAME_CLIENT_NULLCLIENT_WEAPON_RESTORE_H

#include <game/client/avoid/rocket_save.h>

namespace OpenKrx::NullClient
{
	// OpenKRX addition, separate from the recovered Null output policy. A
	// generated Fire counter does not acknowledge a shot on the server.
	class CWeaponRestore
	{
		CRocketWeaponRestore m_State;
		int m_Next = 0, m_Prev = 0;
		bool m_HaveInput = false;

	public:
		void Reset() { *this = {}; }
		int Weapon() const { return m_State.Weapon(); }
		bool NeedsSafeExit() const { return m_State.NeedsSafeExit(); }
		CNetObj_PlayerInput Apply(const CNetObj_PlayerInput &Raw, CNetObj_PlayerInput Output,
			bool GrenadeAction, int PredictedWeapon, int AuthoritativeWeapon,
			int AttackTick, bool SafeWithoutShot)
		{
			const bool ManualCycle = m_HaveInput && (Raw.m_NextWeapon != m_Next || Raw.m_PrevWeapon != m_Prev);
			m_Next = Raw.m_NextWeapon;
			m_Prev = Raw.m_PrevWeapon;
			m_HaveInput = true;
			if(ManualCycle || (m_State.Weapon() >= 0 && Raw.m_WantedWeapon > 0 &&
						  Raw.m_WantedWeapon != WEAPON_GRENADE + 1 && Raw.m_WantedWeapon != m_State.Weapon() + 1))
			{
				m_State.Reset();
				return Output;
			}
			m_State.Observe(AuthoritativeWeapon, AttackTick);
			if(GrenadeAction)
			{
				const int Previous = Raw.m_WantedWeapon > 0 && Raw.m_WantedWeapon != WEAPON_GRENADE + 1 ? Raw.m_WantedWeapon - 1 : AuthoritativeWeapon != WEAPON_GRENADE ? AuthoritativeWeapon :
																							   PredictedWeapon;
				m_State.Prepare(Previous);
			}
			// Null emits +2, including from an even counter. Unlike KRX it does
			// not own a held press/release, so parity is not an issue acknowledgement.
			const bool Issued = GrenadeAction && PredictedWeapon == WEAPON_GRENADE && Raw.m_Fire != Output.m_Fire;
			if(Issued)
				m_State.OnIssued(AttackTick);
			if(!GrenadeAction && !Issued && m_State.NeedsSafeExit() && SafeWithoutShot)
				m_State.OnSafeWithoutShot();
			const int Wanted = m_State.WantedWeapon(Issued, GrenadeAction);
			if(Wanted)
				Output.m_WantedWeapon = Wanted;
			return Output;
		}
	};
}
#endif
