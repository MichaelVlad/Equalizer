
/* Copyright (c) 2006-2011, Stefan Eilemann <eile@equalizergraphics.com> 
 *               2007-2011, Maxim Makhinya  <maxmah@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of Eyescale Software GmbH nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"


namespace eVolve
{

Config::Config( eq::ServerPtr parent )
        : eq::Config( parent )
        , _spinX( 5 )
        , _spinY( 5 )
        , _currentCanvas( 0 )
        , _messageTime( 0 ){}

Config::~Config(){}

bool Config::init()
{
    // init distributed objects
    EQCHECK( registerObject( &_frameData ));

    _frameData.setOrtho( _initData.getOrtho( ));
    _initData.setFrameDataID( _frameData.getID( ));

    _frameData.setAutoObsolete( getLatency( ));

    EQCHECK( registerObject( &_initData ));

    // init config
    if( !eq::Config::init( _initData.getID( )))
    {
        _deregisterData();
        return false;
    }

    const eq::Canvases& canvases = getCanvases();
    if( canvases.empty( ))
        _currentCanvas = 0;
    else
        _currentCanvas = canvases.front();

    _setMessage( "Welcome to eVolve\nPress F1 for help" );

    return true;
}

bool Config::mapData( const eq::uint128_t& initDataID )
{
    if( !_initData.isAttached() )
    {
        if( !mapObject( &_initData, initDataID ))
            return false;
        unmapObject( &_initData ); // data was retrieved, unmap immediately
    }
    else  // appNode, _initData is registered already
    {
        EQASSERT( _initData.getID() == initDataID );
    }
    return true;
}

bool Config::exit()
{
    const bool ret = eq::Config::exit();
    _deregisterData();

    return ret;
}

void Config::_deregisterData()
{
    deregisterObject( &_initData );
    deregisterObject( &_frameData );

    _initData.setFrameDataID( co::base::UUID::ZERO );
}


uint32_t Config::startFrame()
{
    // update database
    _frameData.spinCamera( -0.001f * _spinX, -0.001f * _spinY );
    const co::base::uint128_t& version = _frameData.commit();

    _resetMessage();

    return eq::Config::startFrame( version );
}

void Config::_resetMessage()
{
    // reset message after two seconds
    if( _messageTime > 0 && getTime() - _messageTime > 2000 )
    {
        _messageTime = 0;
        _frameData.setMessage( "" );
    }
}

bool Config::handleEvent( const eq::ConfigEvent* event )
{
    switch( event->data.type )
    {
        case eq::Event::KEY_PRESS:
            if( _handleKeyEvent( event->data.keyPress ))
                return true;
            break;

        case eq::Event::CHANNEL_POINTER_BUTTON_PRESS:
        {
            const co::base::UUID& viewID = event->data.context.view.identifier;
            _frameData.setCurrentViewID( viewID );
            if( viewID == co::base::UUID::ZERO )
            {
                _currentCanvas = 0;
                return true;
            }
            
            const eq::View* view = find< eq::View >( viewID );
            const eq::Layout* layout = view->getLayout();
            const eq::Canvases& canvases = getCanvases();
            for( eq::Canvases::const_iterator i = canvases.begin();
                 i != canvases.end(); ++i )
            {
                eq::Canvas* canvas = *i;
                const eq::Layout* canvasLayout = canvas->getActiveLayout();

                if( canvasLayout == layout )
                {
                    _currentCanvas = canvas;
                    return true;
                }
            }
            return true;
        }

        case eq::Event::CHANNEL_POINTER_BUTTON_RELEASE:
            if( event->data.pointerButtonRelease.buttons == eq::PTR_BUTTON_NONE
                && event->data.pointerButtonRelease.button  == eq::PTR_BUTTON1 )
            {
                _spinY = event->data.pointerButtonRelease.dx;
                _spinX = event->data.pointerButtonRelease.dy;
            }
            return true;

        case eq::Event::CHANNEL_POINTER_MOTION:
            if( event->data.pointerMotion.buttons == eq::PTR_BUTTON_NONE )
                return true;

            if( event->data.pointerMotion.buttons == eq::PTR_BUTTON1 )
            {
                _spinX = 0;
                _spinY = 0;

                _frameData.spinCamera(  -0.005f * event->data.pointerMotion.dy,
                                        -0.005f * event->data.pointerMotion.dx);
            }
            else if( event->data.pointerMotion.buttons == eq::PTR_BUTTON2 ||
                     event->data.pointerMotion.buttons == ( eq::PTR_BUTTON1 |
                                                       eq::PTR_BUTTON3 ))
            {
                _frameData.moveCamera( .0, .0,
                                        .005f*event->data.pointerMotion.dy);
            }
            else if( event->data.pointerMotion.buttons == eq::PTR_BUTTON3 )
            {
                _frameData.moveCamera( .0005f * event->data.pointerMotion.dx,
                                      -.0005f * event->data.pointerMotion.dy, 
                                       .0 );
            }
            return true;

        default:
            break;
    }
    return eq::Config::handleEvent( event );
}

bool Config::_handleKeyEvent( const eq::KeyEvent& event )
{
    switch( event.key )
    {
        case 'b':
        case 'B':
            _frameData.toggleBackground();
            return true;

        case 'd':
        case 'D':
            _frameData.toggleColorMode();
            return true;

        case eq::KC_F1:
        case 'h':
        case 'H':
            _frameData.toggleHelp();
            return true;

        case 'r':
        case 'R':
        case ' ':
            _spinX = 0;
            _spinY = 0;
            _frameData.reset();
            return true;

        case 'n':
        case 'N':
            _frameData.toggleNormalsQuality();
            return true;

        case 'o':
        case 'O':
            _frameData.toggleOrtho();
            return true;

        case 's':
        case 'S':
        _frameData.toggleStatistics();
            return true;

        case 'l':
            _switchLayout( 1 );
            return true;
        case 'L':
            _switchLayout( -1 );
            return true;

        case 'q':
            _frameData.adjustQuality( -.1f );
            return true;

        case 'Q':
            _frameData.adjustQuality( .1f );
            return true;

        case 'c':
        case 'C':
        {
            const eq::Canvases& canvases = getCanvases();
            if( canvases.empty( ))
                return true;

            _frameData.setCurrentViewID( co::base::UUID::ZERO );

            if( !_currentCanvas )
            {
                _currentCanvas = canvases.front();
                return true;
            }

            eq::Canvases::const_iterator i = std::find( canvases.begin(),
                                                        canvases.end(),
                                                        _currentCanvas );
            EQASSERT( i != canvases.end( ));

            ++i;
            if( i == canvases.end( ))
                _currentCanvas = canvases.front();
            else
                _currentCanvas = *i;
            return true;
        }

        case 'v':
        case 'V':
        {
            const eq::Canvases& canvases = getCanvases();
            if( !_currentCanvas && !canvases.empty( ))
                _currentCanvas = canvases.front();

            if( !_currentCanvas )
                return true;

            const eq::Layout* layout = _currentCanvas->getActiveLayout();
            if( !layout )
                return true;

            const eq::View* current = 
                              find< eq::View >( _frameData.getCurrentViewID( ));

            const eq::Views& views = layout->getViews();
            EQASSERT( !views.empty( ))

            if( !current )
            {
                _frameData.setCurrentViewID( views.front()->getID( ));
                return true;
            }

            eq::Views::const_iterator i = std::find( views.begin(),
                                                          views.end(),
                                                          current );
            EQASSERT( i != views.end( ));

            ++i;
            if( i == views.end( ))
                _frameData.setCurrentViewID( co::base::UUID::ZERO );
            else
                _frameData.setCurrentViewID( (*i)->getID( ));
            return true;
        }

        default:
            break;
    }
    return false;
}

void Config::_setMessage( const std::string& message )
{
    _frameData.setMessage( message );
    _messageTime = getTime();
}

void Config::_switchLayout( int32_t increment )
{
    if( !_currentCanvas )
        return;

    _frameData.setCurrentViewID( co::base::UUID::ZERO );

    size_t index = _currentCanvas->getActiveLayoutIndex() + increment;
    const eq::Layouts& layouts = _currentCanvas->getLayouts();
    EQASSERT( !layouts.empty( ));

    index = ( index % layouts.size( ));
    _currentCanvas->useLayout( uint32_t( index ));

    const eq::Layout* layout = _currentCanvas->getActiveLayout();
    std::ostringstream stream;
    stream << "Layout ";
    if( layout )
    {
        const std::string& name = layout->getName();
        if( name.empty( ))
            stream << index;
        else
            stream << name;
    }
    else
        stream << "NONE";

    stream << " active";
    _setMessage( stream.str( ));
}

}
