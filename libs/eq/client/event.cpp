
/* Copyright (c) 2007-2011, Stefan Eilemann <eile@equalizergraphics.com>
 *                    2011, Cedric Stalder <cedric.stalder@gmail.com>  
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

#include "event.h"

#ifdef _WIN32
#  define bzero( ptr, size ) memset( ptr, 0, size );
#else
#  include <strings.h>
#endif

namespace eq
{
namespace
{
/** String representation of event types. */
class EventTypeNames
{
public:
    EventTypeNames()
    {
        _names[Event::WINDOW_EXPOSE] = "window expose";
        _names[Event::WINDOW_RESIZE] = "window resize";
        _names[Event::WINDOW_CLOSE] = "window close";
        _names[Event::WINDOW_HIDE] = "window show";
        _names[Event::WINDOW_SHOW] = "window hide";
        _names[Event::WINDOW_SCREENSAVER] = "window screensaver";
        _names[Event::CHANNEL_POINTER_MOTION] = "pointer motion";
        _names[Event::CHANNEL_POINTER_BUTTON_PRESS] = "pointer button press";
        _names[Event::CHANNEL_POINTER_BUTTON_RELEASE] =
            "pointer button release";
        _names[Event::WINDOW_POINTER_WHEEL] = "pointer wheel";
        _names[Event::WINDOW_POINTER_MOTION] = "window pointer motion";
        _names[Event::WINDOW_POINTER_BUTTON_PRESS] = 
            "window pointer button press";
        _names[Event::WINDOW_POINTER_BUTTON_RELEASE] = 
            "window pointer button release";
        _names[Event::KEY_PRESS] = "key press";
        _names[Event::KEY_RELEASE] = "key release";
        _names[Event::CHANNEL_RESIZE] = "channel resize";
        _names[Event::STATISTIC] = "statistic";
        _names[Event::VIEW_RESIZE] = "view resize";
        _names[Event::EXIT] = "exit";
        _names[Event::MAGELLAN_AXIS] = "magellan axis";
        _names[Event::MAGELLAN_BUTTON] = "magellan button";
        _names[Event::UNKNOWN] = "unknown";
        _names[Event::USER] = "user-specific";
    }

    const std::string &operator[](Event::Type type) const
    {
        return _names[ type ];
    }

private:
    std::string _names[ Event::ALL ];
};
static EventTypeNames _eventTypeNames;
}

Event::Event()
        : type( UNKNOWN )
        , serial( 0 )
        , originator( co::base::UUID::ZERO )
{
    bzero( &user, sizeof( user ));
}

std::ostream& operator << ( std::ostream& os, const Event& event )
{
    os << event.type << ':' << event.originator << ' ';
    switch( event.type )
    {
        case Event::WINDOW_EXPOSE:
        case Event::WINDOW_CLOSE:
            break;

        case Event::WINDOW_RESIZE:
        case Event::WINDOW_SHOW:
        case Event::WINDOW_HIDE:
        case Event::CHANNEL_RESIZE:
        case Event::VIEW_RESIZE:
            os << event.resize;
            break;

        case Event::WINDOW_POINTER_MOTION:
        case Event::WINDOW_POINTER_BUTTON_PRESS:
        case Event::WINDOW_POINTER_BUTTON_RELEASE:
        case Event::WINDOW_POINTER_WHEEL:
        case Event::CHANNEL_POINTER_MOTION:
        case Event::CHANNEL_POINTER_BUTTON_PRESS:
        case Event::CHANNEL_POINTER_BUTTON_RELEASE:
            os << event.pointer;
            break;

        case Event::KEY_PRESS:
        case Event::KEY_RELEASE:
            os << event.key;
            break;

        case Event::STATISTIC:
            os << event.statistic;

        case Event::MAGELLAN_AXIS:
            os << event.magellan;

        default:
            break;
    }
    
    //os << ", context " << event.context <<;
    return os;
}

std::ostream& operator << ( std::ostream& os, const Event::Type& type)
{
    if( type >= Event::ALL )
        os << "unknown (" << static_cast<unsigned>( type ) << ')';
    else 
        os << _eventTypeNames[ type ];

    return os;
}

std::ostream& operator << ( std::ostream& os, const ResizeEvent& event )
{
    os << event.x << 'x' << event.y << '+' << event.w << '+' << event.h << ' ';
    return os;
}

std::ostream& operator << ( std::ostream& os, const PointerEvent& event )
{
    os << '[' << event.x << "], [" << event.y << "] d(" << event.dx << ", "
       << event.dy << ')' << " wheel " << '[' << event.xAxis << ", " 
       << event.yAxis << "] buttons ";

    if( event.buttons == PTR_BUTTON_NONE ) os << "none";
    if( event.buttons & PTR_BUTTON1 ) os << "1";
    if( event.buttons & PTR_BUTTON2 ) os << "2";
    if( event.buttons & PTR_BUTTON3 ) os << "3";
    if( event.buttons & PTR_BUTTON4 ) os << "4";
    if( event.buttons & PTR_BUTTON5 ) os << "5";

    os << " fired ";
    if( event.button == PTR_BUTTON_NONE ) os << "none";
    if( event.button & PTR_BUTTON1 ) os << "1";
    if( event.button & PTR_BUTTON2 ) os << "2";
    if( event.button & PTR_BUTTON3 ) os << "3";
    if( event.button & PTR_BUTTON4 ) os << "4";
    if( event.button & PTR_BUTTON5 ) os << "5";

    os << ' ';
    return os;
}

std::ostream& operator << ( std::ostream& os, const KeyEvent& event )
{
    os << "key " << event.key << ' ';
    return os;
}

std::ostream& operator << ( std::ostream& os, const MagellanEvent& event )
{
    os << " buttons " << event.buttons << " trans " << event.xAxis << ", "
       << event.yAxis << ", " << event.zAxis << " rot " << event.xRotation
       << ", " << event.yRotation << ", " << event.zRotation;
    return os;
}

}
