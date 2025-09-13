#pragma once

#ifndef DEDICATED

// Discord RPC wrapper for Discord Game SDK
// This provides the same interface as the original discord_rpc.h but uses the official Discord Game SDK

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DiscordRichPresence {
    const char* state;   /* max 128 bytes */
    const char* details; /* max 128 bytes */
    int64_t startTimestamp;
    int64_t endTimestamp;
    const char* largeImageKey;  /* max 32 bytes */
    const char* largeImageText; /* max 128 bytes */
    const char* smallImageKey;  /* max 32 bytes */
    const char* smallImageText; /* max 128 bytes */
    const char* partyId;        /* max 128 bytes */
    int partySize;
    int partyMax;
    const char* matchSecret;    /* max 128 bytes */
    const char* joinSecret;     /* max 128 bytes */
    const char* spectateSecret; /* max 128 bytes */
    int8_t instance;
} DiscordRichPresence;

typedef struct DiscordRPCUser {
    const char* userId;
    const char* username;
    const char* discriminator;
    const char* avatar;
} DiscordRPCUser;

typedef void (*readyPtr)(const DiscordRPCUser* request);
typedef void (*disconnectedPtr)(int errorCode, const char* message);
typedef void (*erroredPtr)(int errorCode, const char* message);
typedef void (*joinGamePtr)(const char* joinSecret);
typedef void (*spectateGamePtr)(const char* spectateSecret);
typedef void (*joinRequestPtr)(const DiscordRPCUser* request);

typedef struct DiscordEventHandlers {
    readyPtr ready;
    disconnectedPtr disconnected;
    erroredPtr errored;
    joinGamePtr joinGame;
    spectateGamePtr spectateGame;
    joinRequestPtr joinRequest;
} DiscordEventHandlers;

#define DISCORD_REPLY_NO 0
#define DISCORD_REPLY_YES 1
#define DISCORD_REPLY_IGNORE 2

void Discord_Initialize(const char* applicationId,
                        DiscordEventHandlers* handlers,
                        int autoRegister,
                        const char* optionalSteamId);
void Discord_Shutdown(void);

/* checks for incoming messages, dispatches callbacks */
void Discord_RunCallbacks(void);

void Discord_UpdatePresence(const DiscordRichPresence* presence);
void Discord_ClearPresence(void);

void Discord_Respond(const char* userid, /* DISCORD_REPLY_ */ int reply);

void Discord_UpdateHandlers(DiscordEventHandlers* handlers);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // !DEDICATED
