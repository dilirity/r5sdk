#include "mss.h"

//-----------------------------------------------------------------------------
void MilesGetSubFileInfo( char* const buf, char const* const filename, MilesSubFileInfo_s* const sfi )
{
    sfi->size = 0;
    sfi->start = 0;

    if ( filename[ 0 ] != '*' )
    {
        sfi->filename = filename;
        return;
    }

    char* b;
    char const* f;

    sfi->filename = buf;

    b = buf;
    f = filename + 1;

    while ( ( f[ 0 ] ) && ( f[ 0 ] != '*' ) )
        *b++ = *f++;
    *b = 0;

    if ( f[ 0 ] == '*' )
    {
        U64 v;
        ++f;

        // Read size.
        v = 0;
        while ( ( ( *f ) >= '0' ) && ( ( *f ) <= '9' ) )
            v = ( v * 10 ) + ( ( *f++ ) - '0' );

        sfi->size = v;

        if ( f[ 0 ] == '*' )
        {
            ++f;

            // Read position.
            v = 0;
            while ( ( ( *f ) >= '0' ) && ( ( *f ) <= '9') )
                v = ( v * 10 ) + ( ( *f++ ) - '0' );

            sfi->start = v;
        }
    }
}
