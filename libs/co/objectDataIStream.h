
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

#ifndef CO_OBJECTDATAISTREAM_H
#define CO_OBJECTDATAISTREAM_H

#include <co/dataIStream.h>   // base class
#include <co/version.h>       // enum
#include <co/base/monitor.h>      // member
#include <co/base/thread.h>       // member

#include <deque>

namespace co
{
    /** The DataIStream for object data. */
    class ObjectDataIStream : public DataIStream
    {
    public:
        ObjectDataIStream();
        ObjectDataIStream( const ObjectDataIStream& from );
        virtual ~ObjectDataIStream();

        void addDataPacket( Command& command );
        size_t getDataSize() const;

        virtual uint128_t getVersion() const { return _version.get(); }
        uint128_t getPendingVersion() const;

        void waitReady() const { _version.waitNE( VERSION_INVALID ); }
        bool isReady() const { return _version != VERSION_INVALID; }

        virtual size_t nRemainingBuffers() const  { return _commands.size(); }

        virtual void reset();

        bool hasInstanceData() const;

    protected:
        const Command* getNextCommand();
        virtual bool getNextBuffer( uint32_t* compressor, uint32_t* nChunks,
                                    const void** chunkData, uint64_t* size );

    private:
        /** All data command packets for this istream. */
        CommandDeque _commands;

        Command* _usedCommand; //!< Currently used buffer

        /** The object version associated with this input stream. */
        base::Monitor< uint128_t > _version;

        void _setReady() { _version = getPendingVersion(); }
        void _reset();

        EQ_TS_VAR( _thread );
    };
}
#endif //CO_OBJECTDATAISTREAM_H
