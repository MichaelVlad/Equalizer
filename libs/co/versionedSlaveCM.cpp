
/* Copyright (c) 2007-2011, Stefan Eilemann <eile@equalizergraphics.com> 
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *  
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "versionedSlaveCM.h"

#include "command.h"
#include "commands.h"
#include "log.h"
#include "object.h"
#include "objectDataIStream.h"
#include "objectPackets.h"
#include <co/base/scopedMutex.h>
#include <limits>

namespace co
{
typedef CommandFunc< VersionedSlaveCM > CmdFunc;

VersionedSlaveCM::VersionedSlaveCM( Object* object, uint32_t masterInstanceID )
        : ObjectCM( object )
        , _version( VERSION_NONE )
        , _currentIStream( 0 )
        , _masterInstanceID( masterInstanceID )
#pragma warning(push)
#pragma warning(disable: 4355)
        , _ostream( this )
#pragma warning(pop)
{
    EQASSERT( object );

    object->registerCommand( CMD_OBJECT_INSTANCE,
                             CmdFunc( this, &VersionedSlaveCM::_cmdData ), 0 );
    object->registerCommand( CMD_OBJECT_DELTA,
                             CmdFunc( this, &VersionedSlaveCM::_cmdData ), 0 );
}

VersionedSlaveCM::~VersionedSlaveCM()
{
    while( !_queuedVersions.isEmpty( ))
        delete _queuedVersions.pop();

    EQASSERT( !_currentIStream );
    delete _currentIStream;
    _currentIStream = 0;

    _version = VERSION_NONE;
    _master = 0;
}

uint128_t VersionedSlaveCM::commit( const uint32_t incarnation )
{
#if 0
    EQLOG( LOG_OBJECTS ) << "commit v" << _version << " " << command 
                         << std::endl;
#endif
    if( !_object->isDirty() || !_master || !_master->isConnected( ))
        return VERSION_NONE;

    _ostream.enableCommit( _master );
    _object->pack( _ostream );
    _ostream.disable();

    return _ostream.hasSentData() ? _ostream.getVersion() : VERSION_NONE;
}

uint128_t VersionedSlaveCM::sync( const uint128_t& v )
{
#if 0
    EQLOG( LOG_OBJECTS ) << "sync to v" << v << ", id " << _object->getID()
                         << "." << _object->getInstanceID() << std::endl;
#endif
    if( _version == v )
        return _version;

    if( v == VERSION_HEAD )
    {
        _syncToHead();
        return _version;
    }

    const uint128_t version = ( v == VERSION_NEXT ) ? _version + 1 : v;
    EQASSERTINFO( version.high() == 0, "Not a master version: " << version )
    EQASSERTINFO( _version <= version,
                  "can't sync to older version of object " << 
                  base::className( _object ) << " " << _object->getID() <<
                  " (" << _version << ", " << version <<")" );

    while( _version < version )
        _unpackOneVersion( _queuedVersions.pop( ));

    LocalNodePtr node = _object->getLocalNode();
    if( node.isValid( ))
        node->flushCommands();

    return _version;
}

void VersionedSlaveCM::_syncToHead()
{
    if( _queuedVersions.isEmpty( ))
        return;

    ObjectDataIStream* is = 0;
    while( _queuedVersions.tryPop( is ))
        _unpackOneVersion( is );

    LocalNodePtr localNode = _object->getLocalNode();
    if( localNode.isValid( ))
        localNode->flushCommands();
}

void VersionedSlaveCM::_releaseStream( ObjectDataIStream* stream )
{
#ifdef CO_AGGRESSIVE_CACHING
    stream->reset();
    _iStreamCache.release( stream );
#else
    delete stream;
#endif
}

uint128_t VersionedSlaveCM::getHeadVersion() const
{
    ObjectDataIStream* is = 0;
    if( _queuedVersions.getBack( is ))
    {
        EQASSERT( is );
        return is->getVersion();
    }
    return _version;    
}

void VersionedSlaveCM::_unpackOneVersion( ObjectDataIStream* is )
{
    EQASSERT( is );
    EQASSERTINFO( _version == is->getVersion() - 1, "Expected version " 
                  << _version + 1 << ", got " << is->getVersion() << " for "
                  << *_object );

    if( is->hasInstanceData( ))
        _object->applyInstanceData( *is );
    else
        _object->unpack( *is );

    _version = is->getVersion();

    EQASSERT( _version != VERSION_INVALID );
    EQASSERT( _version != VERSION_NONE );
    EQASSERTINFO( is->getRemainingBufferSize()==0 && is->nRemainingBuffers()==0,
                  "Object " << typeid( *_object ).name() <<
                  " did not unpack all data" );

#if 0
    EQLOG( LOG_OBJECTS ) << "applied v" << _version << ", id "
                         << _object->getID() << "." << _object->getInstanceID()
                         << std::endl;
#endif
    _releaseStream( is );
}


void VersionedSlaveCM::applyMapData( const uint128_t& version )
{
    while( true )
    {
        ObjectDataIStream* is = _queuedVersions.pop();
        if( is->getVersion() == version )
        {
            EQASSERTINFO( is->hasInstanceData(), *_object );

            if( is->hasData( )) // not VERSION_NONE
                _object->applyInstanceData( *is );
            _version = is->getVersion();

            EQASSERT( _version != VERSION_INVALID );
            EQASSERTINFO( !is->hasData(),
                          base::className( _object ) <<
                          " did not unpack all data, " <<
                          is->getRemainingBufferSize() << " bytes, " <<
                          is->nRemainingBuffers() << " buffer(s)" );

            _releaseStream( is );
#if 0
            EQLOG( LOG_OBJECTS ) << "Mapped initial data of " << _object
                                 << std::endl;
#endif
            return;
        }
        else
        {
            // Found the following case:
            // - p1, t1 calls commit
            // - p1, t2 calls mapObject
            // - p1, cmd commits new version
            // - p1, cmd subscribes object
            // - p1, rcv attaches object
            // - p1, cmd receives commit data
            // -> newly attached object recv new commit data before map data,
            //    ignore it
            EQASSERTINFO( is->getVersion() > version,
                          is->getVersion() << " <= " << version );
            _releaseStream( is );
        }
    }
}

void VersionedSlaveCM::addInstanceDatas( const ObjectDataIStreamDeque& cache,
                                         const uint128_t& startVersion )
{
    EQ_TS_THREAD( _rcvThread );
#if 0
    EQLOG( LOG_OBJECTS ) << base::disableFlush << "Adding data front ";
#endif

    uint128_t oldest = VERSION_NONE;
    uint128_t newest = VERSION_NONE;
    if( !_queuedVersions.isEmpty( ))
    {
        ObjectDataIStream* is = 0;

        EQCHECK( _queuedVersions.getFront( is ));
        oldest = is->getVersion();

        EQCHECK( _queuedVersions.getBack( is ));
        newest = is->getVersion();
    }

    ObjectDataIStreamDeque head;
    ObjectDataIStreams tail;

    for( ObjectDataIStreamDeque::const_iterator i = cache.begin();
         i != cache.end(); ++i )
    {
        ObjectDataIStream* stream = *i;
        const uint128_t& version = stream->getVersion();
        if( version < startVersion )
            continue;
        
        EQASSERT( stream->isReady( ));
        EQASSERT( stream->hasInstanceData( ));
        if( !stream->isReady( ))
            break;

        if( version < oldest )
            head.push_front( stream );
        else if( version > newest )
            tail.push_back( stream );
    }

    for( ObjectDataIStreamDeque::const_iterator i = head.begin();
         i != head.end(); ++i )
    {
        const ObjectDataIStream* stream = *i;
#ifndef NDEBUG
        ObjectDataIStream* debugStream = 0;
        _queuedVersions.getFront( debugStream );
        if( debugStream )
            EQASSERT( debugStream->getVersion() == stream->getVersion() + 1);
#endif
        _queuedVersions.pushFront( new ObjectDataIStream( *stream ));
#if 0
        EQLOG( LOG_OBJECTS ) << stream->getVersion() << ' ';
#endif
    }

#if 0
    EQLOG( LOG_OBJECTS ) << " back ";
#endif
    for( ObjectDataIStreams::const_iterator i = tail.begin();
         i != tail.end(); ++i )
    {
        const ObjectDataIStream* stream = *i;
#ifndef NDEBUG
        ObjectDataIStream* debugStream = 0;
        _queuedVersions.getBack( debugStream );
        if( debugStream )
        {
            EQASSERT( debugStream->getVersion() + 1 == stream->getVersion( ));
        } 
#endif
        _queuedVersions.push( new ObjectDataIStream( *stream ));
#if 0
        EQLOG( LOG_OBJECTS ) << stream->getVersion() << ' ';
#endif
    }
#if 0
    EQLOG( LOG_OBJECTS ) << std::endl << base::enableFlush;
#endif
}

//---------------------------------------------------------------------------
// command handlers
//---------------------------------------------------------------------------
bool VersionedSlaveCM::_cmdData( Command& command )
{
    EQ_TS_THREAD( _rcvThread );
    EQASSERT( command.getNode().isValid( ));

    if( !_currentIStream )
        _currentIStream = _iStreamCache.alloc();

    _currentIStream->addDataPacket( command );
    if( _currentIStream->isReady( ))
    {
        const uint128_t& version = _currentIStream->getVersion();
#if 0
        EQLOG( LOG_OBJECTS ) << "v" << version << ", id " << _object->getID()
                             << "." << _object->getInstanceID() << " ready"
                             << std::endl;
#endif
#ifndef NDEBUG
        ObjectDataIStream* debugStream = 0;
        _queuedVersions.getBack( debugStream );
        if ( debugStream )
        {
            EQASSERT( debugStream->getVersion() + 1 == version );
        }
#endif
        _queuedVersions.push( _currentIStream );
        _object->notifyNewHeadVersion( version );
        _currentIStream = 0;
    }
    return true;
}

}
