
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

#include "messagePump.h"

#include "eventHandler.h"
#include "X11Connection.h"

#include <co/base/debug.h>
#include <co/base/log.h>

namespace eq
{
namespace glx
{
MessagePump::MessagePump()
{
}

MessagePump::~MessagePump()
{
}

void MessagePump::postWakeup()
{
    _connections.interrupt();
}

void MessagePump::dispatchOne()
{
    const co::ConnectionSet::Event event = _connections.select();
    switch( event )
    {
        case co::ConnectionSet::EVENT_DISCONNECT:
        {
            co::ConnectionPtr connection = _connections.getConnection();
            _connections.removeConnection( connection );
            EQERROR << "Display connection shut down" << std::endl;
            break;
        }
            
        case co::ConnectionSet::EVENT_DATA:
            EventHandler::dispatch();
            break;

        case co::ConnectionSet::EVENT_INTERRUPT:      
            break;

        case co::ConnectionSet::EVENT_CONNECT:
        case co::ConnectionSet::EVENT_ERROR:      
        default:
            EQWARN << "Error during select" << std::endl;
            break;

        case co::ConnectionSet::EVENT_TIMEOUT:
            break;
    }
}

void MessagePump::dispatchAll()
{
    EventHandler::dispatch();
}

void MessagePump::register_( Display* display )
{
    if( ++_referenced[ display ] == 1 )
        _connections.addConnection( new X11Connection( display ));
}

void MessagePump::deregister( Display* display )
{
    if( --_referenced[ display ] == 0 )
    {
        const co::Connections& connections = _connections.getConnections();
        for( co::Connections::const_iterator i = connections.begin();
             i != connections.end(); ++i )
        {
            co::ConnectionPtr connection = *i;
            const X11Connection* x11Connection =
                static_cast< const X11Connection* >( connection.get( ));
            if( x11Connection->getDisplay() == display )
            {
                _connections.removeConnection( connection );
                break;
            }
        }
        _referenced.erase( _referenced.find( display ));                
    }
}

}
}
