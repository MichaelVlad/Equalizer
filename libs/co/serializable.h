
/* Copyright (c) 2009-2011, Stefan Eilemann <eile@equalizergraphics.com> 
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

#ifndef CO_SERIALIZABLE_H
#define CO_SERIALIZABLE_H

#include <co/object.h>        // base class
#include <co/dataOStream.h>   // used inline
#include <co/dataIStream.h>   // used inline

namespace co
{
    /**
     * Base class for distributed, inheritable objects.
     *
     * This class implements one usage pattern of co::Object, which allows
     * subclassing and serialization of distributed Objects. The implementation
     * hierarchy co::Serializable -> eq::fabric::Object -> eq::fabric::Frustum
     * illustrates the usage of this class.
     */
    class Serializable : public Object
    {
    public:
        /** @return the current dirty bit mask. @version 1.0 */
        uint64_t getDirty() const { return _dirty; }

        /** @return true if the serializable has to be committed. @version 1.0*/
        virtual bool isDirty() const { return ( _dirty != DIRTY_NONE ); }

        /** @return true if the given dirty bit is set. @version 1.0 */
        virtual bool isDirty( const uint64_t dirtyBits ) const
            { return (_dirty & dirtyBits) == dirtyBits; }

        virtual uint128_t commit( const uint32_t incarnation = CO_COMMIT_NEXT )
            {
                const uint128_t& version = co::Object::commit( incarnation );
                _dirty = DIRTY_NONE;
                return version;
            }

    protected:
        /** Construct a new Serializable. @version 1.0 */
        Serializable() : _dirty( DIRTY_NONE ) {}
        
        /**
         * Construct an unmapped, unregistered copy of an serializable.
         * @version 1.0
         */
        Serializable( const Serializable& )
                : co::Object(), _dirty ( DIRTY_NONE ) {}
        
        /** Destruct the serializable. @version 1.0 */
        virtual ~Serializable() {}

        /** 
         * Worker for pack() and getInstanceData().
         *
         * Override this and deserialize() if you want to distribute subclassed
         * data.
         *
         * This method is called with DIRTY_ALL from getInstanceData() and with
         * the actual dirty bits from pack(), which also resets the dirty state
         * afterwards. The dirty bits are transmitted beforehand, and do not
         * need to be transmitted by the overriding method.
         * @version 1.0
         */
        virtual void serialize( co::DataOStream&, const uint64_t ){};

        /** 
         * Worker for unpack() and applyInstanceData().
         * 
         * This function is called with the dirty bits send by the master
         * instance. The dirty bits are received beforehand, and do not need to
         * be deserialized by the overriding method.
         * 
         * @sa serialize()
         * @version 1.0
         */
        virtual void deserialize( co::DataIStream&, const uint64_t ){};

        virtual ChangeType getChangeType() const { return DELTA; }

        /** 
         * The changed parts of the serializable since the last pack().
         *
         * Subclasses should define their own bits, starting at DIRTY_CUSTOM.
         * @version 1.0
         */
        enum DirtyBits
        {
            DIRTY_NONE       = 0,
            DIRTY_CUSTOM     = 1,
            DIRTY_ALL        = 0xFFFFFFFFFFFFFFFFull
        };

        /** Add dirty flags to mark data for distribution. @version 1.0 */
        virtual void setDirty( const uint64_t bits ) { _dirty |= bits; }

        /** Remove dirty flags to clear data from distribution. @version 1.0 */
        virtual void unsetDirty( const uint64_t bits ) { _dirty &= ~bits; }

        virtual void notifyAttached()
            {
                if( isMaster( ))
                    _dirty = DIRTY_NONE;
            }

    private:
        virtual void getInstanceData( co::DataOStream& os )
            {
                serialize( os, DIRTY_ALL );
            }

        virtual void applyInstanceData( co::DataIStream& is )
            {
                EQASSERT( is.hasData( ));
                if( !is.hasData( ))
                    return;
                deserialize( is, DIRTY_ALL );
            }

        virtual void pack( co::DataOStream& os )
            {
                if( _dirty == DIRTY_NONE )
                    return;

                os << _dirty;
                serialize( os, _dirty );
            }

        virtual void unpack( co::DataIStream& is )
            {
                EQASSERT( is.hasData( ));
                if( !is.hasData( ))
                    return;

                uint64_t dirty;
                is >> dirty;
                deserialize( is, dirty );
            }

        /** The current dirty bits. */
        uint64_t _dirty;

        struct Private;
        Private* _private; // placeholder for binary-compatible changes
    };
}
#endif // CO_SERIALIZABLE_H
