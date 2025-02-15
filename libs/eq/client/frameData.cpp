
/* Copyright (c) 2006-2011, Stefan Eilemann <eile@equalizergraphics.com>
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

#include "frameData.h"

#include "nodeStatistics.h"
#include "channelStatistics.h"
#include "exception.h"
#include "image.h"
#include "log.h"
#include "nodePackets.h"
#include "roiFinder.h"

#include <eq/fabric/drawableConfig.h>
#include <co/command.h>
#include <co/commandFunc.h>
#include <co/connectionDescription.h>
#include <co/dataIStream.h>
#include <co/dataOStream.h>
#include <co/base/global.h>
#include <co/base/monitor.h>
#include <co/base/scopedMutex.h>

#include <co/plugins/compressor.h>
#include <algorithm>

namespace eq
{

typedef co::CommandFunc<FrameData> CmdFunc;

FrameData::FrameData() 
        : _version( co::VERSION_NONE.low( ))
        , _useAlpha( true )
        , _colorQuality( 1.f )
        , _depthQuality( 1.f )
        , _colorCompressor( EQ_COMPRESSOR_AUTO )
        , _depthCompressor( EQ_COMPRESSOR_AUTO )
{
    _roiFinder = new ROIFinder();
    EQINFO << "New FrameData @" << (void*)this << std::endl;
}

FrameData::~FrameData()
{
    clear();

    for( Images::const_iterator i = _imageCache.begin();
         i != _imageCache.end(); ++i )
    {
        Image* image = *i;
        EQWARN << "Unflushed image in FrameData destructor" << std::endl;
        delete image;
    }
    _imageCache.clear();

    delete _roiFinder;
    _roiFinder = 0;
}

void FrameData::setQuality( Frame::Buffer buffer, float quality )
{
    if( buffer != Frame::BUFFER_COLOR )
    {
        EQASSERT( buffer == Frame::BUFFER_DEPTH );
        _depthQuality = quality;
        return;
    }

    _colorQuality = quality;
}

void FrameData::useCompressor( const Frame::Buffer buffer, const uint32_t name )
{
    if( buffer != Frame::BUFFER_COLOR )
    {
        EQASSERT( buffer == Frame::BUFFER_DEPTH );
        _depthCompressor = name;
        return;
    }

    _colorCompressor = name;
}

void FrameData::getInstanceData( co::DataOStream& os )
{
    EQUNREACHABLE;
    os << _data;
}

void FrameData::applyInstanceData( co::DataIStream& is )
{
    clear();
    is >> _data;
    EQLOG( LOG_ASSEMBLY ) << "applied " << this << std::endl;
}

void FrameData::clear()
{

    _imageCacheLock.set();
    _imageCache.insert( _imageCache.end(), _images.begin(), _images.end( ));
    _imageCacheLock.unset();

    _images.clear();
}

void FrameData::flush()
{
    clear();

    for( Images::const_iterator i = _imageCache.begin();
         i != _imageCache.end(); ++i )
    {
        Image* image = *i;
        image->flush();
        delete image;
    }

    _imageCache.clear();
}

Image* FrameData::newImage( const eq::Frame::Type type,
                            const DrawableConfig& config )
{
    Image* image = _allocImage( type, config, true /* set quality */ );
    _images.push_back( image );
    return image;
}

Image* FrameData::_allocImage( const eq::Frame::Type type,
                               const DrawableConfig& config,
                               const bool setQuality_ )
{
    Image* image;
    _imageCacheLock.set();

    if( _imageCache.empty( ))
    {
        _imageCacheLock.unset();
        image = new Image;
    }
    else
    {
        image = _imageCache.back();
        _imageCache.pop_back();
        _imageCacheLock.unset();

        image->reset();
    }

    image->setAlphaUsage( _useAlpha );
    image->setStorageType( type );
    if( setQuality_ )
    {
        image->setQuality( Frame::BUFFER_COLOR, _colorQuality );
        image->setQuality( Frame::BUFFER_DEPTH, _depthQuality ); 
    }

    image->useCompressor( Frame::BUFFER_COLOR, _colorCompressor );
    image->useCompressor( Frame::BUFFER_DEPTH, _depthCompressor );

    image->setInternalFormat( Frame::BUFFER_DEPTH,
                              EQ_COMPRESSOR_DATATYPE_DEPTH );
    switch( config.colorBits )
    {
        case 16:  
            image->setInternalFormat( Frame::BUFFER_COLOR, 
                                      EQ_COMPRESSOR_DATATYPE_RGBA16F );
            break;
        case 32:  
            image->setInternalFormat( Frame::BUFFER_COLOR, 
                                      EQ_COMPRESSOR_DATATYPE_RGBA32F );
            break;
        case 10:
            image->setInternalFormat( Frame::BUFFER_COLOR, 
                                      EQ_COMPRESSOR_DATATYPE_RGB10_A2 );
            break;
        default:
            image->setInternalFormat( Frame::BUFFER_COLOR, 
                                      EQ_COMPRESSOR_DATATYPE_RGBA );
    }

    return image;
}

void FrameData::readback( const Frame& frame,
                          util::ObjectManager< const void* >* glObjects,
                          const DrawableConfig& config  )
{
    if( _data.buffers == Frame::BUFFER_NONE )
        return;

    PixelViewport absPVP = _data.pvp + frame.getOffset();
    if( !absPVP.isValid( ))
        return;

    const Zoom& zoom = frame.getZoom();

    if( !zoom.isValid( ))
    {
        EQWARN << "Invalid zoom factor, skipping frame" << std::endl;
        return;
    }

    PixelViewports pvps;

    if( _data.buffers & Frame::BUFFER_DEPTH && zoom == Zoom::NONE )
        pvps = _roiFinder->findRegions( _data.buffers, absPVP, zoom,
//                    frame.getAssemblyStage(), frame.getFrameID(), glObjects );
                    0, 0, glObjects );
    else
        pvps.push_back( absPVP );

    for( uint32_t i = 0; i < pvps.size(); i++ )
    {
        PixelViewport pvp = pvps[ i ];
        pvp.intersect( absPVP );

        Image* image = newImage( _data.frameType, config );
        image->readback( _data.buffers, pvp, zoom, glObjects );
        image->setOffset( pvp.x - absPVP.x, pvp.y - absPVP.y );

#ifndef NDEBUG
        if( getenv( "EQ_DUMP_IMAGES" ))
        {
            static co::base::a_int32_t counter;
            std::ostringstream stringstream;

            stringstream << "Image_" << std::setfill( '0' ) << std::setw(5)
                         << ++counter;
            image->writeImages( stringstream.str( ));
        }
#endif
    }
    setReady();
}

void FrameData::setVersion( const uint64_t version )
{
    EQASSERTINFO( _version <= version, _version << " > " << version );
    _version = version;
    EQLOG( LOG_ASSEMBLY ) << "New v" << version << std::endl;
}

void FrameData::waitReady() const 
{
    // TODO: Use Config::getTimeout() cf. compositor.cpp
    if( co::base::Global::getIAttribute( co::base::Global::IATTR_ROBUSTNESS ) )
    { 
        if( !_readyVersion.timedWaitGE( 
                       _version, co::base::Global::getIAttribute( 
                                    co::base::Global::IATTR_TIMEOUT_DEFAULT )))
        {
            throw Exception( Exception::TIMEOUT_INPUTFRAME );
        }
    }
    else
    {
        _readyVersion.waitGE( _version );
    }
}

void FrameData::setReady()
{
    _setReady( _version );
}

void FrameData::setReady( const NodeFrameDataReadyPacket* packet )
{
    clear();
    EQASSERT(  packet->frameData.version.high() == 0 );
    EQASSERT( _readyVersion < packet->frameData.version.low( ));
    EQASSERT( _readyVersion == 0 ||
              _readyVersion + 1 == packet->frameData.version.low( ));
    EQASSERT( _version == packet->frameData.version.low( ));

    _images.swap( _pendingImages );
    _data = packet->data;
    _setReady( packet->frameData.version.low());

    EQLOG( LOG_ASSEMBLY ) << this << " applied v" 
                          << packet->frameData.version.low() << std::endl;
}

void FrameData::_setReady( const uint64_t version )
{
    
    EQASSERTINFO( _readyVersion <= version,
                  "v" << _version << " ready " << _readyVersion << " new "
                      << version );

    co::base::ScopedMutex< co::base::SpinLock > mutex( _listeners );
    if( _readyVersion >= version )
        return;

    _readyVersion = version;
    EQLOG( LOG_ASSEMBLY ) << "set ready " << this << ", " << _listeners->size()
                          << " monitoring" << std::endl;

    for( Listeners::iterator i= _listeners->begin();
         i != _listeners->end(); ++i )
    {
        Listener* listener = *i;
        ++(*listener);
    }
}

void FrameData::addListener( co::base::Monitor<uint32_t>& listener )
{
    co::base::ScopedMutex< co::base::SpinLock > mutex( _listeners );

    _listeners->push_back( &listener );
    if( _readyVersion >= _version )
        ++listener;
}

void FrameData::removeListener( co::base::Monitor<uint32_t>& listener )
{
    co::base::ScopedMutex< co::base::SpinLock > mutex( _listeners );

    Listeners::iterator i = std::find( _listeners->begin(), _listeners->end(),
                                      &listener );
    EQASSERT( i != _listeners->end( ));
    _listeners->erase( i );
}

bool FrameData::addImage( const NodeFrameDataTransmitPacket* packet )
{
    Image* image = _allocImage( Frame::TYPE_MEMORY, DrawableConfig(),
                                false /* set quality */ );

    // Note on the const_cast: since the PixelData structure stores non-const
    // pointers, we have to go non-const at some point, even though we do not
    // modify the data.
    uint8_t* data  = const_cast< uint8_t* >( packet->data );

    image->setPixelViewport( packet->pvp );
    image->setAlphaUsage( packet->useAlpha );

    Frame::Buffer buffers[] = { Frame::BUFFER_COLOR, Frame::BUFFER_DEPTH };
    for( unsigned i = 0; i < 2; ++i )
    {
        const Frame::Buffer buffer = buffers[i];
        
        if( packet->buffers & buffer )
        {
            PixelData pixelData;
            const ImageHeader* header = reinterpret_cast<ImageHeader*>( data );
            pixelData.internalFormat  = header->internalFormat;
            pixelData.externalFormat  = header->externalFormat;
            pixelData.pixelSize       = header->pixelSize;
            pixelData.pvp             = header->pvp;
            pixelData.compressorName  = header->compressorName;
            pixelData.compressorFlags = header->compressorFlags;
            pixelData.isCompressed =
                pixelData.compressorName > EQ_COMPRESSOR_NONE;

            const uint32_t nChunks    = header->nChunks;
            data += sizeof( ImageHeader );

            if( pixelData.isCompressed )
            {
                pixelData.compressedSize.resize( nChunks );
                pixelData.compressedData.resize( nChunks );

                for( uint32_t j = 0; j < nChunks; ++j )
                {
                    const uint64_t size = *reinterpret_cast< uint64_t*>( data );
                    data += sizeof( uint64_t );
                    
                    pixelData.compressedSize[j] = size; 
                    pixelData.compressedData[j] = data;
                    data += size;
                }
            }
            else
            {
                const uint64_t size = *reinterpret_cast< uint64_t*>( data );
                data += sizeof( uint64_t );
                pixelData.pixels = data;
                data += size;
                EQASSERT( size == pixelData.pvp.getArea()*pixelData.pixelSize );
            }

            image->setQuality( buffer, header->quality );
            image->setPixelData( buffer, pixelData );
        }
    }

    EQASSERT( _readyVersion < packet->frameData.version.low( ));
    EQASSERT( _pendingImages.empty());
    _pendingImages.push_back( image );
    return true;
}

std::ostream& operator << ( std::ostream& os, const FrameData* data )
{
    os << "frame data id " << data->getID() << "." << data->getInstanceID()
       << " v" << data->getVersion() << ' ' << data->getImages().size()
       << " images, ready " << ( data->isReady() ? 'y' :'n' );
    return os;
}

}
