
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

#ifndef CO_OBJECTVERSION_H
#define CO_OBJECTVERSION_H

#include <co/api.h>
#include <co/types.h>
#include <co/base/stdExt.h>

#include <iostream>

namespace co
{
    class Object;

    /** Special object version values */
    extern CO_API const uint128_t VERSION_NONE;
    extern CO_API const uint128_t VERSION_FIRST;
    extern CO_API const uint128_t VERSION_NEXT;
    extern CO_API const uint128_t VERSION_INVALID;
    extern CO_API const uint128_t VERSION_OLDEST;
    extern CO_API const uint128_t VERSION_HEAD;

    /**
     * A helper struct bundling an object identifier and version.
     *
     * The struct either contains the object's identifier and version (if it is
     * registered or mapped), base::UUID::ZERO and VERSION_NONE if it is
     * unmapped or if no object was given.
     */
    struct ObjectVersion
    {
        CO_API ObjectVersion();
        CO_API ObjectVersion( const base::UUID& identifier,
                                 const uint128_t& version );
        CO_API ObjectVersion( const Object* object );
        CO_API ObjectVersion& operator = ( const Object* object );
     
        bool operator == ( const ObjectVersion& value ) const
            {
                return ( identifier == value.identifier &&
                         version == value.version );
            }
        
        bool operator != ( const ObjectVersion& value ) const
            {
                return ( identifier != value.identifier ||
                         version != value.version );
            }
        
        bool operator < ( const ObjectVersion& rhs ) const
            { 
                return identifier < rhs.identifier ||
                    ( identifier == rhs.identifier && version < rhs.version );
            }

        bool operator > ( const ObjectVersion& rhs ) const
            {
                return identifier > rhs.identifier || 
                    ( identifier == rhs.identifier && version > rhs.version );
            }

        uint128_t identifier;
        uint128_t version;

        /** An unset object version. */
        static CO_API ObjectVersion NONE;
    };

    inline std::ostream& operator << (std::ostream& os, const ObjectVersion& ov)
    {
        os << "id " << ov.identifier << " v" << ov.version;
        return os;
    }

}

CO_STDEXT_NAMESPACE_OPEN
#ifdef CO_STDEXT_MSVC
    /** ObjectVersion hash function. */
    template<>
    inline size_t hash_compare< co::ObjectVersion >::operator()
        ( const co::ObjectVersion& key ) const
    {
        const size_t hashVersion = hash_value( key.version );
        const size_t hashID = hash_value( key.identifier );

        return hash_value( hashVersion ^ hashID );
    }
#else
    /** ObjectVersion hash function. */
    template<> struct hash< co::ObjectVersion >
    {
        template< typename P > size_t operator()( const P& key ) const
        {
            return hash< uint64_t >()( hash_value( key.version ) ^ 
                                       hash_value( key.identifier ));
        }
    };
#endif
CO_STDEXT_NAMESPACE_CLOSE

#endif // CO_OBJECT_H
