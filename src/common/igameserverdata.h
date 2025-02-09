//===========================================================================//
//
// Purpose: Enumerations for writing out the requests.
//
//===========================================================================//
#pragma once
#include "netcon/INetCon.h"

typedef int SocketHandle_t;

enum class ServerDataRequestType_e : int
{
	SERVERDATA_REQUEST_VALUE = 0,
	SERVERDATA_REQUEST_SETVALUE,
	SERVERDATA_REQUEST_EXECCOMMAND,
	SERVERDATA_REQUEST_AUTH,
	SERVERDATA_REQUEST_SEND_CONSOLE_LOG,
	SERVERDATA_REQUEST_SEND_REMOTEBUG,
};

enum class ServerDataResponseType_e : int
{
	SERVERDATA_RESPONSE_VALUE = 0,
	SERVERDATA_RESPONSE_UPDATE,
	SERVERDATA_RESPONSE_AUTH,
	SERVERDATA_RESPONSE_CONSOLE_LOG,
	SERVERDATA_RESPONSE_STRING,
	SERVERDATA_RESPONSE_REMOTEBUG,
};

struct ConnectedNetConsoleData_s
{
	SocketHandle_t m_hSocket;
	int  m_nPayloadLen;     // Num bytes for this message.
	int  m_nPayloadRead;    // Num read bytes from input buffer.
	int  m_nFailedAttempts; // Num failed authentication attempts.
	int  m_nIgnoredMessage; // Count how many times client ignored the no-auth message.
	bool m_bValidated;      // Revalidates netconsole if false.
	bool m_bAuthorized;     // Set to true after successful netconsole auth.
	bool m_bInputOnly;      // If set, don't send spew to this netconsole.
	NetConFrameHeader_s m_FrameHeader; // Current frame header.
	vector<byte> m_RecvBuffer;

	ConnectedNetConsoleData_s(SocketHandle_t hSocket = -1)
	{
		m_hSocket = hSocket;
		m_nPayloadLen = 0;
		m_nPayloadRead = 0;
		m_nFailedAttempts = 0;
		m_nIgnoredMessage = 0;
		m_bValidated = false;
		m_bAuthorized = false;
		m_bInputOnly = true;
	}
};

/* PACKET FORMAT **********************************

REQUEST:
  NetConFrameHeader_s header;
  byte* data;

RESPONSE:
  NetConFrameHeader_s header;
  byte* data;

***************************************************/
