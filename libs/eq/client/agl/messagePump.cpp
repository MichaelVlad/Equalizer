
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

#include <eq/client/global.h>
#include <eq/client/os.h>
#include <co/base/debug.h>
#include <co/base/log.h>

namespace eq
{
namespace agl
{

MessagePump::MessagePump()
        : _receiverQueue( 0 )
        , _needGlobalLock( true )
{
    const OSStatus status = CreateEvent( 0, 0, 0, 0, kEventAttributeNone, 
                                         &_wakeupEvent );
    if( status != noErr )
    {
        EQWARN << "CreateEvent failed: " << status << std::endl;
        EQUNREACHABLE;
    }
}

MessagePump::~MessagePump()
{
    ReleaseEvent( _wakeupEvent );
}

void MessagePump::postWakeup()
{
    if( _receiverQueue )
        PostEventToQueue( _receiverQueue, _wakeupEvent, kEventPriorityStandard);
}

void MessagePump::_initReceiverQueue()
{
    if( !_receiverQueue )
    {
        _receiverQueue = GetCurrentEventQueue();
        _needGlobalLock = ( _receiverQueue == GetMainEventQueue( ));
        EQASSERT( _receiverQueue );
    }

    EQASSERTINFO( _receiverQueue == GetCurrentEventQueue(),
                  "MessagePump::dispatch() called from two different threads" );
}

void MessagePump::dispatchOne()
{
    _initReceiverQueue();

    while( true )
    {
        EventRef event;

        if( _needGlobalLock )
            Global::enterCarbon();
            
        const OSStatus status = ReceiveNextEvent( 0, 0, .05 /* 50ms */,
                                                  true, &event );
        if( status == noErr )
        {
            EQVERB << "Dispatch Carbon event " << event << std::endl;

            if( !_needGlobalLock )
                Global::enterCarbon();
            const EventTargetRef target = GetEventDispatcherTarget();
            SendEventToEventTarget( event, target );
            Global::leaveCarbon();

            ReleaseEvent( event );
            return;
        }
        
        if( _needGlobalLock )
            Global::leaveCarbon();

        if( status != eventLoopTimedOutErr )
        {
            EQWARN << "ReceiveNextEvent failed: " << status << std::endl;
            return;
        }
    }
}

void MessagePump::dispatchAll()
{
    _initReceiverQueue();

    while( true )
    {
        EventRef       event;

        if( _needGlobalLock )
            Global::enterCarbon(); 
        const OSStatus status = ReceiveNextEvent( 0, 0, 0.0, true, &event );

        if( status == eventLoopTimedOutErr )
            break;

        if( status != noErr )
        {
            EQWARN << "ReceiveNextEvent failed: " << status << std::endl;
            break;
        }

        EQVERB << "Dispatch Carbon event " << event << std::endl;

        if( !_needGlobalLock )
            Global::enterCarbon();
        const EventTargetRef target = GetEventDispatcherTarget();
        SendEventToEventTarget( event, target );
        Global::leaveCarbon();

        ReleaseEvent( event );
    }

    if( _needGlobalLock )
        Global::leaveCarbon();
}

}
}

