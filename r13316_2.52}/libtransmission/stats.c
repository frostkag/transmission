/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id: stats.c 11709 2011-01-19 13:48:47Z jordan $
 */

#include "transmission.h"
#include "session.h"
#include "bencode.h"
#include "platform.h" /* tr_sessionGetConfigDir() */
#include "stats.h"
#include "utils.h" /* tr_buildPath */

/***
****
***/

static const struct tr_session_stats STATS_INIT = { 0.0f, 0, 0, 0, 0, 0 };

/** @brief Opaque, per-session data structure for bandwidth use statistics */
struct tr_stats_handle
{
    tr_session_stats    single;
    tr_session_stats    old;
    time_t              startTime;
    tr_bool             isDirty;
};

static char*
getOldFilename( const tr_session * session )
{
    return tr_buildPath( tr_sessionGetConfigDir( session ), "stats.benc", NULL );
}

static char*
getFilename( const tr_session * session )
{
    return tr_buildPath( tr_sessionGetConfigDir( session ), "stats.json", NULL );
}

static void
loadCumulativeStats( const tr_session * session,
                     tr_session_stats * setme )
{
    int     loaded = FALSE;
    char   * filename;
    tr_benc top;

    filename = getFilename( session );
    loaded = !tr_bencLoadFile( &top, TR_FMT_JSON, filename );
    tr_free( filename );

    if( !loaded )
    {
        filename = getOldFilename( session );
        loaded = !tr_bencLoadFile( &top, TR_FMT_BENC, filename );
        tr_free( filename );
    }

    if( loaded )
    {
        int64_t i;

        if( tr_bencDictFindInt( &top, "downloaded-bytes", &i ) )
            setme->downloadedBytes = (uint64_t) i;
        if( tr_bencDictFindInt( &top, "files-added", &i ) )
            setme->filesAdded = (uint64_t) i;
        if( tr_bencDictFindInt( &top, "seconds-active", &i ) )
            setme->secondsActive = (uint64_t) i;
        if( tr_bencDictFindInt( &top, "session-count", &i ) )
            setme->sessionCount = (uint64_t) i;
        if( tr_bencDictFindInt( &top, "uploaded-bytes", &i ) )
            setme->uploadedBytes = (uint64_t) i;

        tr_bencFree( &top );
    }
}

static void
saveCumulativeStats( const tr_session * session,
                     const tr_session_stats * s )
{
    char * filename;
    tr_benc top;

    tr_bencInitDict( &top, 5 );
    tr_bencDictAddInt( &top, "downloaded-bytes", s->downloadedBytes );
    tr_bencDictAddInt( &top, "files-added",      s->filesAdded );
    tr_bencDictAddInt( &top, "seconds-active",   s->secondsActive );
    tr_bencDictAddInt( &top, "session-count",    s->sessionCount );
    tr_bencDictAddInt( &top, "uploaded-bytes",   s->uploadedBytes );

    filename = getFilename( session );
    tr_deepLog( __FILE__, __LINE__, NULL, "Saving stats to \"%s\"", filename );
    tr_bencToFile( &top, TR_FMT_JSON, filename );

    tr_free( filename );
    tr_bencFree( &top );
}

/***
****
***/

void
tr_statsInit( tr_session * session )
{
    struct tr_stats_handle * stats = tr_new0( struct tr_stats_handle, 1 );

    loadCumulativeStats( session, &stats->old );
    stats->single.sessionCount = 1;
    stats->startTime = tr_time( );
    session->sessionStats = stats;
}

static struct tr_stats_handle *
getStats( const tr_session * session )
{
    return session ? session->sessionStats : NULL;
}

void
tr_statsSaveDirty( tr_session * session )
{
    struct tr_stats_handle * h = getStats( session );
    if( ( h != NULL ) && h->isDirty )
    {
        tr_session_stats cumulative = STATS_INIT;
        tr_sessionGetCumulativeStats( session, &cumulative );
        saveCumulativeStats( session, &cumulative );
        h->isDirty = FALSE;
    }
}

void
tr_statsClose( tr_session * session )
{
    tr_statsSaveDirty( session );

    tr_free( session->sessionStats );
    session->sessionStats = NULL;
}

/***
****
***/

static void
updateRatio( tr_session_stats * setme )
{
    setme->ratio = tr_getRatio( setme->uploadedBytes,
                                setme->downloadedBytes );
}

static void
addStats( tr_session_stats *       setme,
          const tr_session_stats * a,
          const tr_session_stats * b )
{
    setme->uploadedBytes   = a->uploadedBytes   + b->uploadedBytes;
    setme->downloadedBytes = a->downloadedBytes + b->downloadedBytes;
    setme->filesAdded      = a->filesAdded      + b->filesAdded;
    setme->sessionCount    = a->sessionCount    + b->sessionCount;
    setme->secondsActive   = a->secondsActive   + b->secondsActive;
    updateRatio( setme );
}

void
tr_sessionGetStats( const tr_session * session,
                    tr_session_stats * setme )
{
    const struct tr_stats_handle * stats = getStats( session );
    if( stats )
    {
        *setme = stats->single;
        setme->secondsActive = tr_time( ) - stats->startTime;
        updateRatio( setme );
    }
}

void
tr_sessionGetCumulativeStats( const tr_session * session,
                              tr_session_stats * setme )
{
    const struct tr_stats_handle * stats = getStats( session );
    tr_session_stats current = STATS_INIT;

    if( stats )
    {
        tr_sessionGetStats( session, &current );
        addStats( setme, &stats->old, &current );
    }
}

void
tr_sessionClearStats( tr_session * session )
{
    tr_session_stats zero;

    zero.uploadedBytes = 0;
    zero.downloadedBytes = 0;
    zero.ratio = TR_RATIO_NA;
    zero.filesAdded = 0;
    zero.sessionCount = 0;
    zero.secondsActive = 0;

    session->sessionStats->isDirty = TRUE;
    session->sessionStats->single = session->sessionStats->old = zero;
    session->sessionStats->startTime = tr_time( );
}

/**
***
**/

void
tr_statsAddUploaded( tr_session * session,
                     uint32_t    bytes )
{
    struct tr_stats_handle * s;

    if( ( s = getStats( session ) ) )
    {
        s->single.uploadedBytes += bytes;
        s->isDirty = TRUE;
    }
}

void
tr_statsAddDownloaded( tr_session * session,
                       uint32_t     bytes )
{
    struct tr_stats_handle * s;

    if( ( s = getStats( session ) ) )
    {
        s->single.downloadedBytes += bytes;
        s->isDirty = TRUE;
    }
}

void
tr_statsFileCreated( tr_session * session )
{
    struct tr_stats_handle * s;

    if( ( s = getStats( session ) ) )
        s->single.filesAdded++;
}

