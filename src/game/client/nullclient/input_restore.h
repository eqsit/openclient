#ifndef GAME_CLIENT_NULLCLIENT_INPUT_RESTORE_H
#define GAME_CLIENT_NULLCLIENT_INPUT_RESTORE_H

#include <generated/protocol.h>

namespace OpenKrx::NullClient
{
// Original Controls 140121988..1401219df saves these fields before helpers;
// 140121da6..140121e16 restores them AFTER the emitted/last-input copies.
// Fire and weapon counters deliberately keep their generated values.
class CScopedInputRestore
{
	CNetObj_PlayerInput *m_pInput = nullptr;
	CNetObj_PlayerInput m_Before{};

public:
	CScopedInputRestore() = default;
	CScopedInputRestore(const CScopedInputRestore &) = delete;
	CScopedInputRestore &operator=(const CScopedInputRestore &) = delete;

	void Capture(CNetObj_PlayerInput &Input)
	{
		m_pInput = &Input;
		m_Before = Input;
	}

	~CScopedInputRestore()
	{
		if(!m_pInput)
			return;
		m_pInput->m_Direction = m_Before.m_Direction;
		m_pInput->m_Jump = m_Before.m_Jump;
		m_pInput->m_Hook = m_Before.m_Hook;
		m_pInput->m_TargetX = m_Before.m_TargetX;
		m_pInput->m_TargetY = m_Before.m_TargetY;
		m_pInput->m_PlayerFlags = m_Before.m_PlayerFlags;
	}
};
}

#endif
