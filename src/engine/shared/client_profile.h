#ifndef ENGINE_SHARED_CLIENT_PROFILE_H
#define ENGINE_SHARED_CLIENT_PROFILE_H

#include <engine/shared/config.h>

namespace OpenKrx
{
inline bool g_NullStandalone = false;
inline const char *ClientName() { return g_NullStandalone ? "NullClient" : "OpenClient"; }

// Keep the saved legacy selector so existing beta profiles retain their choice.
// It selects the whole feature pipeline, including input, visuals and networking.
inline bool KrxProfileActive() { return g_Config.m_OpenKrxAvoidProvider == 0; }
inline bool NullProfileActive() { return g_Config.m_OpenKrxAvoidProvider == 1; }

inline const char *SettingsFilename() { return g_NullStandalone ? CONFIG_FILE : OPENCLIENT_CONFIG_FILE; }
inline int &ProfilePredictionMargin()
{
	return !g_NullStandalone && NullProfileActive() ? g_Config.m_NullPredictionMargin : g_Config.m_ClPredictionMargin;
}
}

#endif
