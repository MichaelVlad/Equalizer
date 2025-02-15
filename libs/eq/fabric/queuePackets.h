
/* Copyright (c) 2011, Stefan Eilemann <eile@eyescale.ch>
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

#ifndef EQ_QUEUEPACKETS_H
#define EQ_QUEUEPACKETS_H

#include <co/packets.h> // 'base'
#include "range.h"
#include "pixelViewport.h"
#include "frustum.h"

/** @cond IGNORE */
namespace eq
{
namespace fabric
{

// @bug eile: name QueueFooPacket for all packets

struct ChunkTaskPacket : public co::QueueItemPacket
{
    ChunkTaskPacket()
    {
        size = sizeof(ChunkTaskPacket);
    }

    uint32_t tasks;
    Range range;
};

struct TileTaskPacket : public co::QueueItemPacket
{
    TileTaskPacket()
    {
        size = sizeof(TileTaskPacket);
    }

    uint32_t tasks;
    PixelViewport pvp;
    Viewport vp;
    Frustum frustum;
};

} // fabric
} // eq

/** @endcond */

#endif // EQ_QUEUEPACKETS_H
