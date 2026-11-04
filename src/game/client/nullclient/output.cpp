#include "output.h"

#include <algorithm>

namespace OpenKrx::NullClient
{
	CNetObj_PlayerInput COutputState::Apply(CNetObj_PlayerInput Input, bool Enabled, bool CorrectSlot, bool HasCharacter, int ActiveWeapon, const CSettings &Settings)
	{
		if(!Enabled || !CorrectSlot)
			return Input;
		if(Settings.m_AutoWeapon && Settings.m_RestoreWeapon && m_RestoreWeapon >= 0)
		{
			if((Input.m_WantedWeapon < 1 || Input.m_WantedWeapon == m_RestoreWeapon + 1) && HasCharacter && ActiveWeapon != m_RestoreWeapon)
				Input.m_WantedWeapon = m_RestoreWeapon + 1;
			else
				m_RestoreWeapon = -1;
		}
		if(m_HasVeto && !m_HasPlan)
		{
			if(m_aVetoDirections[std::clamp(Input.m_Direction, -1, 1) + 1])
				Input.m_Direction = 0;
			if(m_VetoJump)
				Input.m_Jump = 0;
			if(m_VetoHook)
				Input.m_Hook = 0;
			if(m_VetoFire)
			{
				if(m_FireCounter & 1U)
					++m_FireCounter;
				Input.m_Fire = static_cast<int>(m_FireCounter);
			}
		}
		if(!(m_HasVeto && m_VetoFire && !m_HasPlan))
			m_FireCounter = static_cast<unsigned>(Input.m_Fire);
		if(m_HasPlan)
		{
			Input.m_Direction = m_Plan.m_Direction;
			Input.m_Jump = m_Plan.m_Jump;
			Input.m_Hook = m_Plan.m_Hook;
			Input.m_TargetX = m_Plan.m_TargetX;
			Input.m_TargetY = m_Plan.m_TargetY;
			if(m_WeaponAction >= 0)
			{
				if(HasCharacter && ActiveWeapon != m_WeaponAction)
				{
					if(Settings.m_AutoWeapon && Settings.m_RestoreWeapon && m_RestoreWeapon < 0 && ActiveWeapon >= 0)
						m_RestoreWeapon = ActiveWeapon;
					Input.m_WantedWeapon = m_WeaponAction + 1;
				}
				else
					Input.m_Fire = static_cast<int>(static_cast<unsigned>(Input.m_Fire) + 2U);
				m_WeaponAction = -1;
			}
			return Input;
		}
		if(m_CutTicks > 0)
		{
			if(m_CutMask & 1U)
				Input.m_Hook = 0;
			if(m_CutMask & 2U)
				Input.m_Jump = 0;
			if(m_CutMask & 4U)
				Input.m_Direction = 0;
		}
		else if(m_HookTicks > 0)
		{
			Input.m_Hook = 1;
			if(m_UseHookAim)
			{
				Input.m_TargetX = m_HookX;
				Input.m_TargetY = m_HookY;
			}
		}
		return Input;
	}
}
