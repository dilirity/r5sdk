//===========================================================================//
//
// Purpose: Net console types
//
//===========================================================================//
#ifndef INETCON_H
#define INETCON_H

#define RCON_FRAME_MAGIC ('R'+('C'<<8)+('o'<<16)+('n'<<24))

// Wire struct for each individual net console frame. Fields are transmitted in
// network byte order and must be flipped to platform's endianness on receive.
struct NetConFrameHeader_s
{
	u32 magic;
	u32 length;
};

#endif // INETCON_H
