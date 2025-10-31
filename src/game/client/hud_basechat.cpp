#include "core/stdafx.h"
#include "engine/client/vengineclient_impl.h"
#include "game/client/cliententitylist.h"
#include "hud_basechat.h"
#include "edict.h"
#include "engine/client/cl_main.h"
#include "localize/localize.h"

static ConVar hudchat_ignore_server_messages("hudchat_ignore_server_messages", "0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Disables server messages appearing in the chat box");

void CBaseHudChat::PrintSystemMsg(const char* const pszPrefixStr, const char* const pszMsgText, const bool bAdminMsg)
{
	if (!bAdminMsg && (!hudchat_visibility->GetBool() || hudchat_ignore_server_messages.GetBool()))
		return;

	static Color adminColour(186, 13, 13, 255);
	const float flMessageShowDuration = hudchat_new_message_shown_duration->GetFloat();
	const float flMessageFadeDuration = hudchat_new_message_fade_duration->GetFloat();

	m_pChatHistory->InsertChar('\n');

	// Create new format context first, then apply fade
	m_pChatHistory->InsertColorChange(bAdminMsg ? adminColour : m_clrText);
	m_pChatHistory->InsertFade(flMessageShowDuration, flMessageFadeDuration);

	m_pChatHistory->InsertText(pszPrefixStr);
	m_pChatHistory->InsertText(L": ");

	m_pChatHistory->InsertColorChange(m_clrText);

	m_pChatHistory->InsertText(pszMsgText);
}

void CBaseHudChat::PrintSystemMsg(const char* const pszPrefixStr, const char* const pszMsgText, const int r, const int g, const int b)
{
	// RGB overload always shows messages (bypasses visibility check)
	const float flMessageShowDuration = hudchat_new_message_shown_duration->GetFloat();
	const float flMessageFadeDuration = hudchat_new_message_fade_duration->GetFloat();

	Color customColor(r, g, b, 255);

	m_pChatHistory->InsertChar('\n');

	// Create new format context first, then apply fade
	m_pChatHistory->InsertColorChange(customColor);
	m_pChatHistory->InsertFade(flMessageShowDuration, flMessageFadeDuration);

	m_pChatHistory->InsertText(pszPrefixStr);
	m_pChatHistory->InsertText(L": ");

	m_pChatHistory->InsertColorChange(m_clrText);

	m_pChatHistory->InsertText(pszMsgText);
}

void CBaseHudChat::PrintSystemMsg(const char* const pszPrefixStr, const char* const pszMsgText, const int r, const int g, const int b, const float flDuration, const float flFadeTime)
{
	// RGB overload with custom timing - always shows messages (bypasses visibility check)
	Color customColor(r, g, b, 255);

	m_pChatHistory->InsertChar('\n');

	// Create new format context first, then apply fade
	m_pChatHistory->InsertColorChange(customColor);
	m_pChatHistory->InsertFade(flDuration, flFadeTime);

	// Insert text without ": " separator since we want full control
	m_pChatHistory->InsertText(pszPrefixStr);

	// If message is provided and not empty, add it with default color
	if (pszMsgText && pszMsgText[0] != '\0')
	{
		m_pChatHistory->InsertColorChange(m_clrText);
		m_pChatHistory->InsertText(pszMsgText);
	}
}

void CBaseHudChat::ProcessChatBuilderCommands(const char* pszCommands)
{
	// Process ChatBuilder commands safely as a member function
	// Format: "CMD|data|CMD|data|..."
	// Commands: N=newline, T=text, C=color(r,g,b), F=fade(dur,fade), R=rainbow text

	const char* cmd = pszCommands;
	bool bNeedsFadeReset = false; // Track if we need to reset fade after newline
	bool bFirstNewline = true;    // Track first newline to close previous context

	// Rainbow color palette
	static const Color rainbowColors[] = {
		Color(255, 0, 0, 255),     // Red
		Color(255, 127, 0, 255),   // Orange
		Color(255, 255, 0, 255),   // Yellow
		Color(0, 255, 0, 255),     // Green
		Color(0, 0, 255, 255),     // Blue
		Color(75, 0, 130, 255),    // Indigo
		Color(148, 0, 211, 255)    // Violet
	};
	static const int numRainbowColors = sizeof(rainbowColors) / sizeof(rainbowColors[0]);

	while (cmd && *cmd)
	{
		// Skip whitespace
		while (*cmd == ' ' || *cmd == '\t') cmd++;

		if (*cmd == '\0')
			break;

		char cmdType = *cmd;
		cmd++; // Skip command char

		if (*cmd != '|')
			break; // Malformed
		cmd++; // Skip |

		switch (cmdType)
		{
			case 'N': // Newline
			{
				// First newline in a ChatBuilder message: ensure we close any previous context
				// by setting a new color AND fade before the newline. This prevents fade inheritance from user messages.
				if (bFirstNewline)
				{
					// Get default fade timing
					const float flMessageShowDuration = hudchat_new_message_shown_duration->GetFloat();
					const float flMessageFadeDuration = hudchat_new_message_fade_duration->GetFloat();

					// Close previous context with a new format context that has proper fade
					m_pChatHistory->InsertColorChange(m_clrText);
					m_pChatHistory->InsertFade(flMessageShowDuration, flMessageFadeDuration);
					bFirstNewline = false;
				}

				m_pChatHistory->InsertChar('\n');
				// Mark that we need a fade reset before any other operations
				// This ensures the next F| command will apply to this new message
				bNeedsFadeReset = true;
				break;
			}

			case 'T': // Text
			{
				// If text comes right after newline without fade, apply default fade
				if (bNeedsFadeReset)
				{
					// Use default fade timing from ConVars
					const float flMessageShowDuration = hudchat_new_message_shown_duration->GetFloat();
					const float flMessageFadeDuration = hudchat_new_message_fade_duration->GetFloat();

					m_pChatHistory->InsertColorChange(m_clrText);
					m_pChatHistory->InsertFade(flMessageShowDuration, flMessageFadeDuration);
					bNeedsFadeReset = false;
				}

				// Find end marker
				const char* textEnd = strchr(cmd, '|');
				if (!textEnd)
					textEnd = cmd + strlen(cmd); // Rest of string

				char textBuf[256];
				size_t textLen = textEnd - cmd;
				if (textLen >= sizeof(textBuf))
					textLen = sizeof(textBuf) - 1;

				memcpy(textBuf, cmd, textLen);
				textBuf[textLen] = '\0';

				m_pChatHistory->InsertText(textBuf);

				cmd = (*textEnd == '|') ? textEnd + 1 : textEnd;
				break;
			}

			case 'C': // Color (r,g,b)
			{
				int r = 255, g = 255, b = 255;
				sscanf(cmd, "%d,%d,%d", &r, &g, &b);

				// Clamp values
				r = (r < 0) ? 0 : ((r > 255) ? 255 : r);
				g = (g < 0) ? 0 : ((g > 255) ? 255 : g);
				b = (b < 0) ? 0 : ((b > 255) ? 255 : b);

				Color color(r, g, b, 255);
				m_pChatHistory->InsertColorChange(color);

				// Color change starts a new format context, so clear the flag
				bNeedsFadeReset = false;

				// Skip to next |
				cmd = strchr(cmd, '|');
				if (cmd) cmd++;
				break;
			}

			case 'F': // Fade (duration,fadeTime)
			{
				float duration = 5.0f, fadeTime = 1.0f;
				sscanf(cmd, "%f,%f", &duration, &fadeTime);

				// Minimum fade time is 0.1 seconds
				if (fadeTime < 0.1f)
					fadeTime = 0.1f;

				// If this is right after a newline, set a color first to create new format context
				if (bNeedsFadeReset)
				{
					m_pChatHistory->InsertColorChange(m_clrText);
					bNeedsFadeReset = false;
				}

				// Now apply fade - it will apply to the new format context we just created
				m_pChatHistory->InsertFade(duration, fadeTime);

				// Skip to next |
				cmd = strchr(cmd, '|');
				if (cmd) cmd++;
				break;
			}

			case 'R': // Rainbow text (auto-colors each character)
			{
				// If rainbow comes right after newline without fade, apply default fade
				if (bNeedsFadeReset)
				{
					const float flMessageShowDuration = hudchat_new_message_shown_duration->GetFloat();
					const float flMessageFadeDuration = hudchat_new_message_fade_duration->GetFloat();

					m_pChatHistory->InsertColorChange(m_clrText);
					m_pChatHistory->InsertFade(flMessageShowDuration, flMessageFadeDuration);
					bNeedsFadeReset = false;
				}

				// Find end marker
				const char* textEnd = strchr(cmd, '|');
				if (!textEnd)
					textEnd = cmd + strlen(cmd);

				// Limit rainbow text to 50 characters max to prevent overflow
				size_t textLen = textEnd - cmd;
				if (textLen > 50)
					textLen = 50;

				// Apply rainbow colors per character
				for (size_t i = 0; i < textLen; i++)
				{
					// Set color for this character
					const Color& color = rainbowColors[i % numRainbowColors];
					m_pChatHistory->InsertColorChange(color);

					// Insert single character
					char singleChar[2] = { cmd[i], '\0' };
					m_pChatHistory->InsertText(singleChar);
				}

				cmd = (*textEnd == '|') ? textEnd + 1 : textEnd;
				break;
			}

			default:
				// Unknown command, skip to next |
				cmd = strchr(cmd, '|');
				if (cmd) cmd++;
				break;
		}
	}
}