
/* Copyright (c) 2009-2011, Stefan Eilemann <eile@equalizergraphics.com>
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

#include "view.h"

#include "canvas.h"
#include "channel.h"
#include "compound.h"
#include "config.h"
#include "configDestCompoundVisitor.h"
#include "layout.h"
#include "log.h"
#include "observer.h"
#include "segment.h"
#include "equalizers/equalizer.h"

#include <eq/client/viewPackets.h>
#include <eq/fabric/paths.h>
#include <co/dataIStream.h>
#include <co/dataOStream.h>

namespace eq
{
namespace server
{
typedef fabric::View< Layout, View, Observer > Super;
typedef co::CommandFunc<View> ViewFunc;

View::View( Layout* parent )
        : Super( parent )
{
}

View::~View()
{
    // Use copy - Channel::unsetOutput modifies vector
    Channels channels = _channels;
    for( Channels::const_iterator i = channels.begin();
         i != channels.end(); ++i )
    {
        Channel* channel = *i;
        channel->unsetOutput();
    }

    EQASSERT( _channels.empty( ));
    _channels.clear();
}

void View::attach( const UUID& id, const uint32_t instanceID )
{
    Super::attach( id, instanceID );

    co::CommandQueue* mainQ = getServer()->getMainThreadQueue();
    registerCommand( fabric::CMD_VIEW_FREEZE_LOAD_BALANCING, 
                     ViewFunc( this, &View::_cmdFreezeLoadBalancing ), mainQ );
}

namespace
{
class FrustumUpdater : public ConfigVisitor
{
public:
    FrustumUpdater( const Channels& channels, const Vector3f& eye,
                    const float ratio )
            : _channels( channels )
            , _eye( eye )
            , _ratio( ratio )
        {}
    virtual ~FrustumUpdater() {}

    virtual VisitorResult visit( Compound* compound )
        {
            const Channel* channel = compound->getChannel();
            if( !channel )
                return TRAVERSE_CONTINUE;

            if( !compound->isDestination( ))
                return TRAVERSE_PRUNE; // only change destination compounds

            if( std::find( _channels.begin(), _channels.end(), channel ) !=
                _channels.end( )) // our destination channel
            {
                compound->updateFrustum( _eye, _ratio );
            }

            return TRAVERSE_PRUNE;
        }
private:
    const Channels& _channels;
    const Vector3f& _eye;
    const float _ratio;
};

class CapabilitiesUpdater : public ConfigVisitor
{
public:
    CapabilitiesUpdater( View* view )
            : _view( view )
            , _capabilities( _view->getMaximumCapabilities( ))
        {}

    virtual ~CapabilitiesUpdater(){}

    virtual VisitorResult visit( Compound* compound )
    {
        const Channel* dest = compound->getInheritChannel();
        if( !dest || dest->getView() != _view )
            return TRAVERSE_CONTINUE;

        const Channel* src = compound->getChannel();
        if( !src->supportsView( _view ))
            return TRAVERSE_CONTINUE;

        const uint64_t supported = src->getCapabilities();
        _capabilities &= supported;
        return TRAVERSE_CONTINUE;
    }

    uint64_t getCapabilities() const { return _capabilities; }

private:
    View* const _view;
    uint64_t _capabilities;
};

class FreezeVisitor : public ConfigVisitor
{
public:
    // No need to go down on nodes.
    virtual VisitorResult visitPre( Node* node ) { return TRAVERSE_PRUNE; }

    FreezeVisitor( const View* view, const bool freeze )
            : _view( view ), _freeze( freeze )
        {}

    virtual VisitorResult visit( Compound* compound )
    {
        const Channel* dest = compound->getInheritChannel();
        if( !dest )
            return TRAVERSE_CONTINUE;

        if( dest->getView() != _view )
            return TRAVERSE_PRUNE;

        const Equalizers& equalizers = compound->getEqualizers();
        for( Equalizers::const_iterator i = equalizers.begin();
            i != equalizers.end(); ++i )
        {
            (*i)->setFrozen( _freeze );
        }
        return TRAVERSE_CONTINUE; 
    }

private:
    const View* const _view;
    const bool _freeze;
};

}

void View::setDirty( const uint64_t bits )
{
    if( bits == 0 || !isAttached( ))
        return;

    Super::setDirty( bits );
    _updateChannels();
}

void View::_updateChannels() const
{
    EQASSERT( isMaster( ));
    co::ObjectVersion version( this );
    if( isDirty( ))
        ++version.version;
        
    for( Channels::const_iterator i = _channels.begin();
         i != _channels.end(); ++i )
    {
        Channel* channel = *i;
        channel->setViewVersion( version );
    }
}

void View::deserialize( co::DataIStream& is, const uint64_t dirtyBits )
{
    EQASSERT( isMaster( ));
    Super::deserialize( is, dirtyBits );

    if( dirtyBits & ( DIRTY_FRUSTUM | DIRTY_OVERDRAW ))
        updateFrusta();
}

Config* View::getConfig()
{
    Layout* layout = getLayout();
    EQASSERT( layout );
    return layout ? layout->getConfig() : 0;
}

const Config* View::getConfig() const
{
    const Layout* layout = getLayout();
    EQASSERT( layout );
    return layout ? layout->getConfig() : 0;
}

ServerPtr View::getServer()
{
    Config* config = getConfig();
    EQASSERT( config );
    return ( config ? config->getServer() : 0 );
}

void View::addChannel( Channel* channel )
{
    _channels.push_back( channel );
}

bool View::removeChannel( Channel* channel )
{
    Channels::iterator i = stde::find( _channels, channel );

    EQASSERT( i != _channels.end( ));
    if( i == _channels.end( ))
        return false;

    _channels.erase( i );
    return true;
}

ViewPath View::getPath() const
{
    const Layout* layout = getLayout();
    EQASSERT( layout );
    ViewPath path( layout->getPath( ));
    
    const Views& views = layout->getViews();
    Views::const_iterator i = std::find( views.begin(), views.end(), this );
    EQASSERT( i != views.end( ));
    path.viewIndex = std::distance( views.begin(), i );
    return path;
}

void View::trigger( const Canvas* canvas, const bool active )
{
    const Mode mode = getMode();
    Config* config = getConfig();

    // (De)activate destination compounds for canvas/eye(s)
    for( Channels::const_iterator i = _channels.begin(); 
         i != _channels.end(); ++i )
    {
        Channel* channel = *i;
        const Canvas* channelCanvas = channel->getCanvas();
        const Layout* canvasLayout = channelCanvas->getActiveLayout();

        if((  canvas && channelCanvas != canvas ) ||
           ( !canvas && canvasLayout  != getLayout( )))
        {
            continue;
        }

        const Segment* segment = channel->getSegment();
        const uint32_t segmentEyes = segment->getEyes();
        const uint32_t eyes = ( mode == MODE_MONO ) ?
                           EYE_CYCLOP & segmentEyes : EYES_STEREO & segmentEyes;
        if( eyes == 0 )
            continue;

        ConfigDestCompoundVisitor visitor( channel, true /*activeOnly*/ );
        config->accept( visitor );     

        const Compounds& compounds = visitor.getResult();
        for( Compounds::const_iterator j = compounds.begin(); 
             j != compounds.end(); ++j )
        {
            Compound* compound = *j;
            if( active )
            {
                compound->activate( eyes );
                EQLOG( LOG_VIEW ) << "Activate " << compound->getName()
                                  << std::endl;
            }
            else
            {
                compound->deactivate( eyes );
                EQLOG( LOG_VIEW ) << "Deactivate " << compound->getName()
                                  << std::endl;
            }
        }
    }
}

void View::activateMode( const Mode mode )
{
    if( getMode() == mode )
        return;

    Config* config = getConfig();
    if( config->isRunning( ))
    {
        config->postNeedsFinish();
        trigger( 0, false );
    }

    Super::activateMode( mode );

    if( config->isRunning( ))
        trigger( 0, true );
}

void View::updateCapabilities()
{
    CapabilitiesUpdater visitor( this );
    getConfig()->accept( visitor );
    setCapabilities( visitor.getCapabilities( ));
}

void View::updateFrusta()
{
    const Channels& channels = getChannels();
    Vector3f eye;
    const float ratio = _computeFocusRatio( eye );

    Config* config = getConfig();
    FrustumUpdater updater( channels, eye, ratio );

    config->accept( updater );
}

float View::_computeFocusRatio( Vector3f& eye )
{
    eye = Vector3f::ZERO;
    const Observer* observer = getObserver();
    const FocusMode mode = observer ? observer->getFocusMode() :FOCUSMODE_FIXED;
    if( mode == FOCUSMODE_FIXED )
        return 1.f;

    const Channels& channels = getChannels();
    if( channels.empty( ))
        return 1.f;

    Vector4f view4 = Vector4f::FORWARD;
    if( mode == FOCUSMODE_RELATIVE_TO_OBSERVER )
    {
        view4 = observer->getHeadMatrix() * view4;
        eye = observer->getEyePosition( EYE_CYCLOP );
    }
    Vector3f view = view4;
    view.normalize();

    float distance = std::numeric_limits< float >::max();
    if( getCurrentType() != Frustum::TYPE_NONE ) // frustum from view
    {
        const Wall& wall = getWall();
        const Vector3f w = wall.getW();
        const float denom = view.dot( w );
        if( denom != 0.f ) // view parallel to wall
        {
            const float d = (wall.bottomLeft - eye).dot( w ) / denom;
            if( d > 0.f )
                distance = d;
        }
    }
    else
    {
        // Find closest segment and its distance from cyclop eye
        for( ChannelsCIter i = channels.begin(); i != channels.end(); ++i )
        {
            Segment* segment = (*i)->getSegment();
            if( segment->getCurrentType() == Frustum::TYPE_NONE )
            {
                segment->notifyFrustumChanged();
                if( segment->getCurrentType() == Frustum::TYPE_NONE )
                    continue;
            }

            // http://en.wikipedia.org/wiki/Line-plane_intersection
            const Wall& wall = segment->getWall();
            const Vector3f w = wall.getW();
            const float denom = view.dot( w );
            if( denom == 0.f ) // view parallel to wall
                continue;

            const float d = (wall.bottomLeft - eye).dot( w ) / denom;
            if( d > distance || d <= 0.f ) // further away or behind
                continue;

            distance = d;
            //EQINFO << "Eye " << eye << " is " << d << " from " << wall
            // << std::endl;
        }
    }

    float focusDistance = observer->getFocusDistance();
    if( mode == FOCUSMODE_RELATIVE_TO_ORIGIN )
    {
        eye = observer->getEyePosition( EYE_CYCLOP );

        if( distance != std::numeric_limits< float >::max( ))
        {
            distance += eye.z();
            focusDistance += eye.z();
            if( fabsf( distance ) <= std::numeric_limits< float >::epsilon( ))
                distance = 2.f * std::numeric_limits< float >::epsilon();
        }
    }

    if( distance == std::numeric_limits< float >::max( ))
        return 1.f;
    return focusDistance / distance;
}

bool View::_cmdFreezeLoadBalancing( co::Command& command ) 
{
    const ViewFreezeLoadBalancingPacket* packet = 
        command.get<ViewFreezeLoadBalancingPacket>();

    FreezeVisitor visitor( this, packet->freeze );
    getConfig()->accept( visitor );

    return true;
}

}
}

#include "../fabric/view.ipp"

template class eq::fabric::View< eq::server::Layout, eq::server::View,
                                 eq::server::Observer >;
/** @cond IGNORE */
template std::ostream& eq::fabric::operator << ( std::ostream&,
                         const eq::fabric::View< eq::server::Layout,
                                                 eq::server::View,
                                                 eq::server::Observer >& );
/** @endcond */
