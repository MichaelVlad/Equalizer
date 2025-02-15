
/* Copyright (c) 2005-2011, Stefan Eilemann <eile@equalizergraphics.com>
 *                    2010, Cedric Stalder <cedric.stalder@gmail.com>
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

#include "objectStore.h"

#include "barrier.h"
#include "command.h"
#include "connection.h"
#include "connectionDescription.h"
#include "global.h"
#include "instanceCache.h"
#include "log.h"
#include "nodePackets.h"
#include "objectCM.h"
#include "objectDataIStream.h"
#include "objectPackets.h"

#include <co/base/scopedMutex.h>

//#define DEBUG_DISPATCH
#ifdef DEBUG_DISPATCH
#  include <set>
#endif

namespace co
{
typedef CommandFunc<ObjectStore> CmdFunc;

ObjectStore::ObjectStore( LocalNode* localNode )
        : _localNode( localNode )
        , _instanceIDs( std::numeric_limits< long >::min( )) 
        , _instanceCache( new InstanceCache( Global::getIAttribute( 
                              Global::IATTR_INSTANCE_CACHE_SIZE ) * EQ_1MB ) )
{
    EQASSERT( localNode );
    CommandQueue* queue = localNode->getCommandThreadQueue();

    localNode->_registerCommand( CMD_NODE_FIND_MASTER_NODE_ID,
        CmdFunc( this, &ObjectStore::_cmdFindMasterNodeID ), queue );
    localNode->_registerCommand( CMD_NODE_FIND_MASTER_NODE_ID_REPLY,
        CmdFunc( this, &ObjectStore::_cmdFindMasterNodeIDReply ), 0 );
    localNode->_registerCommand( CMD_NODE_ATTACH_OBJECT,
        CmdFunc( this, &ObjectStore::_cmdAttachObject ), 0 );
    localNode->_registerCommand( CMD_NODE_DETACH_OBJECT,
        CmdFunc( this, &ObjectStore::_cmdDetachObject ), 0 );
    localNode->_registerCommand( CMD_NODE_REGISTER_OBJECT,
        CmdFunc( this, &ObjectStore::_cmdRegisterObject ), queue );
    localNode->_registerCommand( CMD_NODE_DEREGISTER_OBJECT,
        CmdFunc( this, &ObjectStore::_cmdDeregisterObject ), queue );
    localNode->_registerCommand( CMD_NODE_MAP_OBJECT,
        CmdFunc( this, &ObjectStore::_cmdMapObject ), queue );
    localNode->_registerCommand( CMD_NODE_MAP_OBJECT_SUCCESS,
        CmdFunc( this, &ObjectStore::_cmdMapObjectSuccess ), 0 );
    localNode->_registerCommand( CMD_NODE_MAP_OBJECT_REPLY,
        CmdFunc( this, &ObjectStore::_cmdMapObjectReply ), 0 );
    localNode->_registerCommand( CMD_NODE_UNMAP_OBJECT,
        CmdFunc( this, &ObjectStore::_cmdUnmapObject ), 0 );
    localNode->_registerCommand( CMD_NODE_UNSUBSCRIBE_OBJECT,
        CmdFunc( this, &ObjectStore::_cmdUnsubscribeObject ), queue );
    localNode->_registerCommand( CMD_NODE_OBJECT_INSTANCE,
        CmdFunc( this, &ObjectStore::_cmdInstance ), 0 );
    localNode->_registerCommand( CMD_NODE_OBJECT_INSTANCE_MAP,
        CmdFunc( this, &ObjectStore::_cmdInstance ), 0 );
    localNode->_registerCommand( CMD_NODE_OBJECT_INSTANCE_COMMIT,
        CmdFunc( this, &ObjectStore::_cmdInstance ), 0 );
    localNode->_registerCommand( CMD_NODE_OBJECT_INSTANCE_PUSH,
        CmdFunc( this, &ObjectStore::_cmdInstance ), 0 );
    localNode->_registerCommand( CMD_NODE_DISABLE_SEND_ON_REGISTER,
        CmdFunc( this, &ObjectStore::_cmdDisableSendOnRegister ), queue );
    localNode->_registerCommand( CMD_NODE_REMOVE_NODE,
        CmdFunc( this, &ObjectStore::_cmdRemoveNode ), queue );
    localNode->_registerCommand( CMD_NODE_OBJECT_PUSH,
        CmdFunc( this, &ObjectStore::_cmdObjectPush ), queue );
}

ObjectStore::~ObjectStore()
{
    EQVERB << "Delete ObjectStore @" << (void*)this << std::endl;
    
#ifndef NDEBUG
    if( !_objects->empty( ))
    {
        EQWARN << _objects->size() << " attached objects in destructor"
               << std::endl;
        
        for( ObjectsHash::const_iterator i = _objects->begin();
             i != _objects->end(); ++i )
        {
            const Objects& objects = i->second;
            EQWARN << "  " << objects.size() << " objects with id " 
                   << i->first << std::endl;
            
            for( Objects::const_iterator j = objects.begin();
                 j != objects.end(); ++j )
            {
                const Object* object = *j;
                EQINFO << "    object type " << base::className( object )
                       << std::endl;
            }
        }
    }
    //EQASSERT( _objects->empty( ))
#endif
   clear();
   delete _instanceCache;
   _instanceCache = 0;
}

void ObjectStore::clear( )
{
    EQASSERT( _objects->empty( ));
    expireInstanceData( 0 );
    EQASSERT( !_instanceCache || _instanceCache->isEmpty( ));

    _objects->clear();
    _sendQueue.clear();
}

void ObjectStore::disableInstanceCache()
{
    EQASSERT( _localNode->isClosed( ));
    delete _instanceCache;
    _instanceCache = 0;
}

void ObjectStore::expireInstanceData( const int64_t age )
{
    if( _instanceCache )
        _instanceCache->expire( age ); 
}

void ObjectStore::removeInstanceData( const NodeID& nodeID )
{
    if( _instanceCache )
        _instanceCache->remove( nodeID ); 
}

void ObjectStore::enableSendOnRegister()
{
    ++_sendOnRegister;
}

void ObjectStore::disableSendOnRegister()
{
    if( Global::getIAttribute( Global::IATTR_NODE_SEND_QUEUE_SIZE ) > 0 )
    {
        NodeDisableSendOnRegisterPacket packet;
        packet.requestID = _localNode->registerRequest();
        _localNode->send( packet );
        _localNode->waitRequest( packet.requestID );
    }
    else // OPT
        --_sendOnRegister;
}

//---------------------------------------------------------------------------
// identifier master node mapping
//---------------------------------------------------------------------------
NodeID ObjectStore::_findMasterNodeID( const base::UUID& identifier )
{
    EQ_TS_NOT_THREAD( _commandThread );

    // OPT: look up locally first?
    Nodes nodes;
    _localNode->getNodes( nodes );
    
    // OPT: send to multiple nodes at once?
    for( Nodes::iterator i = nodes.begin(); i != nodes.end(); i++ )
    {
        NodePtr node = *i;
        EQLOG( LOG_OBJECTS ) << "Finding " << identifier << " on " << node
                             << std::endl;

        NodeFindMasterNodeIDPacket packet;
        packet.requestID = _localNode->registerRequest();
        packet.identifier = identifier;
        node->send( packet );

        NodeID masterNodeID = base::UUID::ZERO;
        _localNode->waitRequest( packet.requestID, masterNodeID );
        if( masterNodeID != base::UUID::ZERO )
        {
            EQLOG( LOG_OBJECTS ) << "Found " << identifier << " on "
                                 << masterNodeID << std::endl;
            return masterNodeID;
        }
    }

    return base::UUID::ZERO;
}

//---------------------------------------------------------------------------
// object mapping
//---------------------------------------------------------------------------
void ObjectStore::attachObject( Object* object, const base::UUID& id, 
                                const uint32_t instanceID )
{
    EQASSERT( object );
    EQ_TS_NOT_THREAD( _receiverThread );

    NodeAttachObjectPacket packet;
    packet.requestID = _localNode->registerRequest( object );
    packet.objectID = id;
    packet.objectInstanceID = instanceID;

    _localNode->send( packet );
    _localNode->waitRequest( packet.requestID );
}

namespace
{
uint32_t _genNextID( base::a_int32_t& val )
{
    uint32_t result;
    do
    {
        const long id = ++val;
        result = static_cast< uint32_t >(
            static_cast< int64_t >( id ) + 0x7FFFFFFFu );
    }
    while( result > EQ_INSTANCE_MAX );

    return result;
}
}

void ObjectStore::_attachObject( Object* object, const base::UUID& id, 
                                 const uint32_t inInstanceID )
{
    EQASSERT( object );
    EQ_TS_THREAD( _receiverThread );

    uint32_t instanceID = inInstanceID;
    if( inInstanceID == EQ_INSTANCE_INVALID )
        instanceID = _genNextID( _instanceIDs );

    object->attach( id, instanceID );

    {
        base::ScopedMutex< base::SpinLock > mutex( _objects );
        Objects& objects = _objects.data[ id ];
        EQASSERTINFO( !object->isMaster() || objects.empty(),
            "Attaching master " << *object << ", " << objects.size() <<
            " attached objects with same ID, first is: " << *objects[0] );
        objects.push_back( object );
    }

    _localNode->flushCommands(); // redispatch pending commands

    EQLOG( LOG_OBJECTS ) << "attached " << *object << " @" 
                         << static_cast< void* >( object ) << std::endl;
}

void ObjectStore::detachObject( Object* object )
{
    EQASSERT( object );
    EQ_TS_NOT_THREAD( _receiverThread );

    NodeDetachObjectPacket packet;
    packet.requestID = _localNode->registerRequest();
    packet.objectID  = object->getID();
    packet.objectInstanceID  = object->getInstanceID();

    _localNode->send( packet );
    _localNode->waitRequest( packet.requestID );
}

void ObjectStore::swapObject( Object* oldObject, Object* newObject )
{
    EQASSERT( newObject );
    EQASSERT( oldObject );
    EQASSERT( oldObject->isMaster() );
    EQ_TS_THREAD( _receiverThread );

    if( !oldObject->isAttached() )
        return;

    EQLOG( LOG_OBJECTS ) << "Swap " << base::className( oldObject ) <<std::endl;
    const base::UUID& id = oldObject->getID();

    base::ScopedMutex< base::SpinLock > mutex( _objects );
    ObjectsHash::iterator i = _objects->find( id );
    EQASSERT( i != _objects->end( ));
    if( i == _objects->end( ))
        return;

    Objects& objects = i->second;
    Objects::iterator j = find( objects.begin(), objects.end(), oldObject );
    EQASSERT( j != objects.end( ));
    if( j == objects.end( ))
        return;

    newObject->transfer( oldObject );
    *j = newObject;
}

void ObjectStore::_detachObject( Object* object )
{
    // check also _cmdUnmapObject when modifying!
    EQASSERT( object );
    EQ_TS_THREAD( _receiverThread );

    if( !object->isAttached() )
        return;

    const base::UUID& id = object->getID();

    EQASSERT( _objects->find( id ) != _objects->end( ));
    EQLOG( LOG_OBJECTS ) << "Detach " << *object << std::endl;

    Objects& objects = _objects.data[ id ];
    Objects::iterator i = find( objects.begin(),objects.end(), object );
    EQASSERT( i != objects.end( ));

    {
        base::ScopedMutex< base::SpinLock > mutex( _objects );
        objects.erase( i );
        if( objects.empty( ))
            _objects->erase( id );
    }

    EQASSERT( object->getInstanceID() != EQ_INSTANCE_INVALID );
    object->detach();
    return;
}

uint32_t ObjectStore::mapObjectNB( Object* object, const ObjectVersion& v )
{
    return mapObjectNB( object, v.identifier, v.version );
}

uint32_t ObjectStore::mapObjectNB( Object* object, const base::UUID& id,
                                   const uint128_t& version )
{
    EQ_TS_NOT_THREAD( _commandThread );
    EQ_TS_NOT_THREAD( _receiverThread );
    EQLOG( LOG_OBJECTS ) << "Mapping " << base::className( object ) << " to id "
                         << id << " version " << version << std::endl;
    EQASSERT( object );
    EQASSERT( !object->isAttached( ));
    EQASSERT( !object->isMaster( ));
    EQASSERT( !_localNode->inCommandThread( ));
    EQASSERTINFO( id.isGenerated(), id );

    object->notifyAttach();
    if( !id.isGenerated( ))
        return EQ_UNDEFINED_UINT32;

    NodePtr master = _connectMaster( id );
    EQASSERT( master );
    return mapObjectNB( object, id, version, master );
}

uint32_t ObjectStore::mapObjectNB( Object* object, const base::UUID& id, 
                                   const uint128_t& version, NodePtr master )
{
    if( !master || !master->isConnected( ))
    {
        EQWARN << "Mapping of object " << id << " failed, invalid master node"
               << std::endl;
        return EQ_UNDEFINED_UINT32;
    }

    NodeMapObjectPacket packet;
    packet.requestID        = _localNode->registerRequest( object );
    packet.objectID         = id;
    packet.requestedVersion = version;
    packet.instanceID       = _genNextID( _instanceIDs );

    if( _instanceCache )
    {
        const InstanceCache::Data& cached = (*_instanceCache)[ id ];
        if( cached != InstanceCache::Data::NONE )
        {
            const ObjectDataIStreamDeque& versions = cached.versions;
            EQASSERT( !cached.versions.empty( ));
            packet.useCache = true;
            packet.masterInstanceID = cached.masterInstanceID;
            packet.minCachedVersion = versions.front()->getVersion();
            packet.maxCachedVersion = versions.back()->getVersion();
            EQLOG( LOG_OBJECTS ) << "Object " << id << " have v"
                                 << packet.minCachedVersion << ".."
                                 << packet.maxCachedVersion << std::endl;
        }
    }
    master->send( packet );
    return packet.requestID;
}

bool ObjectStore::mapObjectSync( const uint32_t requestID )
{
    if( requestID == EQ_UNDEFINED_UINT32 )
        return false;

    void* data = _localNode->getRequestData( requestID );    
    if( data == 0 )
        return false;

    Object* object = EQSAFECAST( Object*, data );
    uint128_t version = VERSION_NONE;
    _localNode->waitRequest( requestID, version );

    const bool mapped = object->isAttached();
    if( mapped )
        object->applyMapData( version ); // apply initial instance data

    object->notifyAttached();
    EQLOG( LOG_OBJECTS ) << "Mapped " << base::className( object ) << std::endl;
    return mapped;
}

void ObjectStore::unmapObject( Object* object )
{
    EQASSERT( object );
    if( !object->isAttached( )) // not registered
        return;

    const base::UUID& id = object->getID();
    
    EQLOG( LOG_OBJECTS ) << "Unmap " << object << std::endl;

    object->notifyDetach();

    // send unsubscribe to master, master will send detach packet.
    EQASSERT( !object->isMaster( ));
    EQ_TS_NOT_THREAD( _commandThread );
    
    const uint32_t masterInstanceID = object->getMasterInstanceID();
    if( masterInstanceID != EQ_INSTANCE_INVALID )
    {
        NodePtr master = object->getMasterNode();
        EQASSERT( master )

        if( master.isValid() && master->isConnected( ))
        {
            NodeUnsubscribeObjectPacket packet;
            packet.requestID = _localNode->registerRequest();
            packet.objectID  = id;
            packet.masterInstanceID = masterInstanceID;
            packet.slaveInstanceID  = object->getInstanceID();
            master->send( packet );

            _localNode->waitRequest( packet.requestID );
            object->notifyDetached();
            return;
        }
        EQERROR << "Master node for object id " << id << " not connected"
                << std::endl;
    }

    // no unsubscribe sent: Detach directly
    detachObject( object );
    object->setupChangeManager( Object::NONE, false, 0, EQ_INSTANCE_INVALID );
    object->notifyDetached();
}

bool ObjectStore::registerObject( Object* object )
{
    EQASSERT( object );
    EQASSERT( !object->isAttached() );

    const base::UUID& id = object->getID( );
    EQASSERTINFO( id.isGenerated(), id );

    object->notifyAttach();
    object->setupChangeManager( object->getChangeType(), true, _localNode,
                                EQ_INSTANCE_INVALID );
    attachObject( object, id, EQ_INSTANCE_INVALID );

    if( Global::getIAttribute( Global::IATTR_NODE_SEND_QUEUE_SIZE ) > 0 )
    {
        NodeRegisterObjectPacket packet;
        packet.object = object;
        _localNode->send( packet );
    }

    object->notifyAttached();

    EQLOG( LOG_OBJECTS ) << "Registered " << object << std::endl;
    return true;
}

void ObjectStore::deregisterObject( Object* object )
{
    EQASSERT( object );
    if( !object->isAttached( )) // not registered
        return;

    EQLOG( LOG_OBJECTS ) << "Deregister " << *object << std::endl;
    EQASSERT( object->isMaster( ));

    object->notifyDetach();

    if( Global::getIAttribute( Global::IATTR_NODE_SEND_QUEUE_SIZE ) > 0  )
    {
        // remove from send queue
        NodeDeregisterObjectPacket packet;
        packet.requestID = _localNode->registerRequest( object );
        _localNode->send( packet );
        _localNode->waitRequest( packet.requestID );
    }

    const base::UUID id = object->getID();
    detachObject( object );
    object->setupChangeManager( Object::NONE, true, 0, EQ_INSTANCE_INVALID );
    if( _instanceCache )
        _instanceCache->erase( id );
    object->notifyDetached();
}

NodePtr ObjectStore::_connectMaster( const base::UUID& id )
{
    const NodeID masterNodeID = _findMasterNodeID( id );
    if( masterNodeID == base::UUID::ZERO )
    {
        EQWARN << "Can't find master node for object id " << id <<std::endl;
        return 0;
    }

    NodePtr master = _localNode->connect( masterNodeID );
    if( master.isValid() && !master->isClosed( ))
        return master;

    EQWARN << "Can't connect master node with id " << masterNodeID
           << " for object id " << id << std::endl;
    return 0;
}

bool ObjectStore::notifyCommandThreadIdle()
{
    EQ_TS_THREAD( _commandThread );
    if( _sendQueue.empty( ))
        return false;

    EQASSERT( _sendOnRegister > 0 );
    SendQueueItem& item = _sendQueue.front();

    if( item.age > _localNode->getTime64( ))
    {
        Nodes nodes;
        _localNode->getNodes( nodes, false );
        if( nodes.empty( ))
        {
            base::Thread::yield();
            return !_sendQueue.empty();
        }

        item.object->sendInstanceData( nodes );
    }
    _sendQueue.pop_front();
    return !_sendQueue.empty();
}

void ObjectStore::removeNode( NodePtr node )
{
    NodeRemoveNodePacket packet;
    packet.node = node.get();
    packet.requestID = _localNode->registerRequest( );
    _localNode->send( packet );
    _localNode->waitRequest( packet.requestID );
}

//===========================================================================
// Packet handling
//===========================================================================
bool ObjectStore::dispatchObjectCommand( Command& command )
{
    EQ_TS_THREAD( _receiverThread );
    const ObjectPacket* packet = command.get< ObjectPacket >();
    const base::UUID& id = packet->objectID;
    const uint32_t instanceID = packet->instanceID;

    ObjectsHash::const_iterator i = _objects->find( id );

    if( i == _objects->end( ))
        // When the instance ID is set to none, we only care about the packet
        // when we have an object of the given ID (multicast)
        return ( instanceID == EQ_INSTANCE_NONE );

    const Objects& objects = i->second;
    EQASSERTINFO( !objects.empty(), packet );

    if( instanceID <= EQ_INSTANCE_MAX )
    {
        for( Objects::const_iterator j = objects.begin(); j!=objects.end(); ++j)
        {
            Object* object = *j;
            if( instanceID == object->getInstanceID( ))
            {
                EQCHECK( object->dispatchCommand( command ));
                return true;
            }
        }
        EQUNREACHABLE;
        return false;
    }

    Objects::const_iterator j = objects.begin();
    Object* object = *j;
    EQCHECK( object->dispatchCommand( command ));

    for( ++j; j != objects.end(); ++j )
    {
        object = *j;
        Command& clone = _localNode->cloneCommand( command );
        EQCHECK( object->dispatchCommand( clone ));
    }
    return true;
}

bool ObjectStore::_cmdFindMasterNodeID( Command& command )
{
    EQ_TS_THREAD( _commandThread );

    const NodeFindMasterNodeIDPacket* packet = 
          command.get<NodeFindMasterNodeIDPacket>();

    const base::UUID& id = packet->identifier;
    EQASSERT( id.isGenerated() );

    NodeFindMasterNodeIDReplyPacket reply( packet );
    {
        base::ScopedMutex< base::SpinLock > mutex( _objects );
        ObjectsHash::const_iterator i = _objects->find( id );

        if( i != _objects->end( ))
        {
            const Objects& objects = i->second;
            EQASSERTINFO( !objects.empty(), packet );

            for( ObjectsCIter j = objects.begin(); j != objects.end(); ++j )
            {
                Object* object = *j;
                if( object->isMaster( ))
                    reply.masterNodeID = _localNode->getNodeID();
                else
                {
                    NodePtr master = object->getMasterNode();
                    if( master.isValid( ))
                        reply.masterNodeID = master->getNodeID();
                }
                if( reply.masterNodeID != base::UUID::ZERO )
                    break;
            }
    
            EQLOG( LOG_OBJECTS ) << "Found object " << id << " master:"
                                 << reply.masterNodeID << std::endl;
        }
        else
            EQLOG( LOG_OBJECTS ) << "Object " << id << " unknown" << std::endl;
    }

    command.getNode()->send( reply );
    return true;
}

bool ObjectStore::_cmdFindMasterNodeIDReply( Command& command )
{
    const NodeFindMasterNodeIDReplyPacket* packet =
          command.get< NodeFindMasterNodeIDReplyPacket >();
    _localNode->serveRequest( packet->requestID, packet->masterNodeID );
    return true;
}

bool ObjectStore::_cmdAttachObject( Command& command )
{
    EQ_TS_THREAD( _receiverThread );
    const NodeAttachObjectPacket* packet =
        command.get< NodeAttachObjectPacket >();
    EQLOG( LOG_OBJECTS ) << "Cmd attach object " << packet << std::endl;

    Object* object = static_cast< Object* >( _localNode->getRequestData( 
                                                 packet->requestID ));
    _attachObject( object, packet->objectID, packet->objectInstanceID );
    _localNode->serveRequest( packet->requestID );
    return true;
}

bool ObjectStore::_cmdDetachObject( Command& command )
{
    EQ_TS_THREAD( _receiverThread );
    const NodeDetachObjectPacket* packet =
        command.get< NodeDetachObjectPacket >();
    EQLOG( LOG_OBJECTS ) << "Cmd detach object " << packet << std::endl;

    const base::UUID& id = packet->objectID;
    ObjectsHash::const_iterator i = _objects->find( id );
    if( i != _objects->end( ))
    {
        const Objects& objects = i->second;

        for( Objects::const_iterator j = objects.begin();
             j != objects.end(); ++j )
        {
            Object* object = *j;
            if( object->getInstanceID() == packet->objectInstanceID )
            {
                _detachObject( object );
                break;
            }
        }
    }

    EQASSERT( packet->requestID != EQ_UNDEFINED_UINT32 );
    _localNode->serveRequest( packet->requestID );
    return true;
}

bool ObjectStore::_cmdRegisterObject( Command& command )
{
    EQ_TS_THREAD( _commandThread );
    if( _sendOnRegister <= 0 )
        return true;

    const NodeRegisterObjectPacket* packet = 
        command.get< NodeRegisterObjectPacket >();
    EQLOG( LOG_OBJECTS ) << "Cmd register object " << packet << std::endl;

    const int32_t age = Global::getIAttribute(
                            Global::IATTR_NODE_SEND_QUEUE_AGE );
    SendQueueItem item;
    item.age = age ? age + _localNode->getTime64() :
                     std::numeric_limits< int64_t >::max();
    item.object = packet->object;
    _sendQueue.push_back( item );

    const uint32_t size = Global::getIAttribute( 
                             Global::IATTR_NODE_SEND_QUEUE_SIZE );
    while( _sendQueue.size() > size )
        _sendQueue.pop_front();

    return true;
}

bool ObjectStore::_cmdDeregisterObject( Command& command )
{
    EQ_TS_THREAD( _commandThread );
    const NodeDeregisterObjectPacket* packet = 
        command.get< NodeDeregisterObjectPacket >();
    EQLOG( LOG_OBJECTS ) << "Cmd deregister object " << packet << std::endl;

    const void* object = _localNode->getRequestData( packet->requestID ); 

    for( SendQueue::iterator i = _sendQueue.begin(); i < _sendQueue.end(); ++i )
    {
        if( i->object == object )
        {
            _sendQueue.erase( i );
            break;
        }
    }

    _localNode->serveRequest( packet->requestID );
    return true;
}

bool ObjectStore::_cmdMapObject( Command& command )
{
    EQ_TS_THREAD( _commandThread );
    const NodeMapObjectPacket* packet = command.get< NodeMapObjectPacket >();
    EQLOG( LOG_OBJECTS ) << "Cmd map object " << packet << std::endl;

    NodePtr node = command.getNode();
    const base::UUID& id = packet->objectID;
    Object* master = 0;
    {
        base::ScopedMutex< base::SpinLock > mutex( _objects );
        ObjectsHash::const_iterator i = _objects->find( id );
        if( i != _objects->end( ))
        {
            const Objects& objects = i->second;

            for( Objects::const_iterator j = objects.begin();
                 j != objects.end(); ++j )
            {
                Object* object = *j;
                if( object->isMaster( ))
                {
                    master = object;
                    break;
                }
            }
        }
    }
    
    NodeMapObjectReplyPacket reply( packet );
    reply.nodeID = node->getNodeID();

    if( master )
    {
        // Check requested version
        NodeMapObjectSuccessPacket successPacket( packet );
        successPacket.changeType       = master->getChangeType();
        successPacket.masterInstanceID = master->getInstanceID();
        successPacket.nodeID = node->getNodeID();

        // Prefer multicast connection, since this will be used by the CM as
        // well. If we send the packet on another connection, it might arrive
        // after the packets below
        if( !node->multicast( successPacket ))
            node->send( successPacket );
        
        master->addSlave( command, reply );
        reply.result = true;
    }
    else
        EQWARN << "Can't find master object to map " << id << std::endl;

    if( !node->multicast( reply ))
        node->send( reply );
    return true;
}

bool ObjectStore::_cmdMapObjectSuccess( Command& command )
{
    EQ_TS_THREAD( _receiverThread );
    const NodeMapObjectSuccessPacket* packet = 
        command.get<NodeMapObjectSuccessPacket>();

    // Map success packets are potentially multicasted (see above)
    // verify that we are the intended receiver
    if( packet->nodeID != _localNode->getNodeID( ))
        return true;

    EQLOG( LOG_OBJECTS ) << "Cmd map object success " << packet << std::endl;

    // set up change manager and attach object to dispatch table
    Object* object = static_cast<Object*>( _localNode->getRequestData( 
                                               packet->requestID ));    
    EQASSERT( object );
    EQASSERT( !object->isMaster( ));

    object->setupChangeManager( Object::ChangeType( packet->changeType ), false,
                                _localNode, packet->masterInstanceID );
    _attachObject( object, packet->objectID, packet->instanceID );
    return true;
}

bool ObjectStore::_cmdMapObjectReply( Command& command )
{
    EQ_TS_THREAD( _receiverThread );
    const NodeMapObjectReplyPacket* packet = 
        command.get< NodeMapObjectReplyPacket >();
    EQLOG( LOG_OBJECTS ) << "Cmd map object reply " << packet << std::endl;

    // Map reply packets are potentially multicasted (see above)
    // verify that we are the intended receiver
    if( packet->nodeID != _localNode->getNodeID( ))
        return true;

    EQASSERT( _localNode->getRequestData( packet->requestID ));

    if( packet->result )
    {
        Object* object = static_cast<Object*>( 
            _localNode->getRequestData( packet->requestID ));    
        EQASSERT( object );
        EQASSERT( !object->isMaster( ));

        object->setMasterNode( command.getNode( ));

        if( packet->useCache )
        {
            EQASSERT( packet->releaseCache );
            EQASSERT( _instanceCache );

            const base::UUID& id = packet->objectID;
            const InstanceCache::Data& cached = (*_instanceCache)[ id ];
            EQASSERT( cached != InstanceCache::Data::NONE );
            EQASSERT( !cached.versions.empty( ));
            
            object->addInstanceDatas( cached.versions, packet->version );
            EQCHECK( _instanceCache->release( id, 2 ));
        }
        else if( packet->releaseCache )
        {
            EQCHECK( _instanceCache->release( packet->objectID, 1 ));
        }
    }
    else
    {
        if( packet->releaseCache )
            _instanceCache->release( packet->objectID, 1 );

        EQWARN << "Could not map object " << packet->objectID << std::endl;
    }

    _localNode->serveRequest( packet->requestID, packet->version );
    return true;
}

bool ObjectStore::_cmdUnsubscribeObject( Command& command )
{
    EQ_TS_THREAD( _commandThread );
    const NodeUnsubscribeObjectPacket* packet =
        command.get< NodeUnsubscribeObjectPacket >();
    EQLOG( LOG_OBJECTS ) << "Cmd unsubscribe object  " << packet << std::endl;

    NodePtr node = command.getNode();
    const base::UUID& id = packet->objectID;

    {
        base::ScopedMutex< base::SpinLock > mutex( _objects );
        ObjectsHash::const_iterator i = _objects->find( id );
        if( i != _objects->end( ))
        {
            const Objects& objects = i->second;

            for( Objects::const_iterator j = objects.begin();
                 j != objects.end(); ++j )
            {
                Object* object = *j;
                if( object->isMaster() && 
                    object->getInstanceID() == packet->masterInstanceID )
                {
                    object->removeSlave( node );
                    break;
                }
            }   
        }
    }

    NodeDetachObjectPacket detachPacket( packet );
    node->send( detachPacket );
    return true;
}

bool ObjectStore::_cmdUnmapObject( Command& command )
{
    EQ_TS_THREAD( _receiverThread );
    const NodeUnmapObjectPacket* packet = 
        command.get< NodeUnmapObjectPacket >();

    EQLOG( LOG_OBJECTS ) << "Cmd unmap object " << packet << std::endl;
    if( _instanceCache )
        _instanceCache->erase( packet->objectID );

    ObjectsHash::iterator i = _objects->find( packet->objectID );
    if( i == _objects->end( )) // nothing to do
        return true;

    const Objects objects = i->second;
    {
        base::ScopedMutex< base::SpinLock > mutex( _objects );
        _objects->erase( i );
    }

    for( Objects::const_iterator j = objects.begin(); j != objects.end(); ++j )
    {
        Object* object = *j;
        object->detach();
    }

    return true;
}

bool ObjectStore::_cmdInstance( Command& command )
{
    EQ_TS_THREAD( _receiverThread );
    EQASSERT( _localNode );

    ObjectInstancePacket* packet =
        command.getModifiable< ObjectInstancePacket >();
    EQLOG( LOG_OBJECTS ) << "Cmd instance " << packet << std::endl;

    const uint32_t type = packet->command;

    packet->type = PACKETTYPE_CO_OBJECT;
    packet->command = CMD_OBJECT_INSTANCE;
    const ObjectVersion rev( packet->objectID, packet->version ); 

    if( _instanceCache )
        _instanceCache->add( rev, packet->masterInstanceID, command, 0 );

    switch( type )
    {
      case CMD_NODE_OBJECT_INSTANCE:
        EQASSERT( packet->nodeID == NodeID::ZERO );
        EQASSERT( packet->instanceID == EQ_INSTANCE_NONE );
        return true;

      case CMD_NODE_OBJECT_INSTANCE_MAP:
        if( packet->nodeID != _localNode->getNodeID( )) // not for me
            return true;

        EQASSERT( packet->instanceID <= EQ_INSTANCE_MAX );
        return dispatchObjectCommand( command );

      case CMD_NODE_OBJECT_INSTANCE_COMMIT:
        EQASSERT( packet->nodeID == NodeID::ZERO );
        EQASSERT( packet->instanceID == EQ_INSTANCE_NONE );
        return dispatchObjectCommand( command );

      case CMD_NODE_OBJECT_INSTANCE_PUSH:
        EQASSERT( packet->nodeID == NodeID::ZERO );
        EQASSERT( packet->instanceID == EQ_INSTANCE_NONE );
        _pushData.addDataPacket( packet->objectID, command );
        return true;

      default:
        EQUNREACHABLE;
        return false;
    }
}

bool ObjectStore::_cmdDisableSendOnRegister( Command& command )
{
    EQ_TS_THREAD( _commandThread );
    EQASSERTINFO( _sendOnRegister > 0, _sendOnRegister );

    if( --_sendOnRegister == 0 )
    {
        _sendQueue.clear();

        Nodes nodes;
        _localNode->getNodes( nodes, false );
        for( NodesCIter i = nodes.begin(); i != nodes.end(); ++i )
        {
            NodePtr node = *i;
            ConnectionPtr connection = node->getMulticast();
            if( connection.isValid( ))
                connection->finish();
        }
    }

    const NodeDisableSendOnRegisterPacket* packet =
        command.get< NodeDisableSendOnRegisterPacket >();
    _localNode->serveRequest( packet->requestID );
    return true;
}

bool ObjectStore::_cmdRemoveNode( Command& command )
{
    EQ_TS_THREAD( _commandThread );
    const NodeRemoveNodePacket* packet = command.get< NodeRemoveNodePacket >();

    EQLOG( LOG_OBJECTS ) << "Cmd  object  " << packet << std::endl;

    base::ScopedMutex< base::SpinLock > mutex( _objects );
    for ( ObjectsHashCIter i = _objects->begin(); i != _objects->end(); ++i )
    {
        const Objects& objects = i->second;
        for( ObjectsCIter j = objects.begin(); j != objects.end(); ++j )
            (*j)->removeSlaves( packet->node );
    }

    if( packet->requestID != EQ_UNDEFINED_UINT32 )
        _localNode->serveRequest( packet->requestID );

    return true;
}

bool ObjectStore::_cmdObjectPush( Command& command )
{
    EQ_TS_THREAD( _commandThread );
    const NodeObjectPushPacket* packet = command.get< NodeObjectPushPacket >();
    ObjectDataIStream* is = _pushData.pull( packet->objectID );
    
    _localNode->objectPush( packet->groupID, packet->typeID, packet->objectID,
                            *is );
    _pushData.recycle( is );
    return true;
}

std::ostream& operator << ( std::ostream& os, ObjectStore* objectStore )
{
    if( !objectStore )
    {
        os << "NULL objectStore";
        return os;
    }
    
    os << "objectStore (" << (void*)objectStore << ")";

    return os;
}
}
