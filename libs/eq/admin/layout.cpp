
/* Copyright (c) 2010-2011, Stefan Eilemann <eile@eyescale.ch>
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

#include "layout.h"

#include "canvas.h"
#include "config.h"
#include "nodeFactory.h"
#include "view.h"
#include "server.h"

namespace eq
{
namespace admin
{
typedef fabric::Layout< Config, Layout, View > Super;

Layout::Layout( Config* parent )
        : Super( parent )
{
}

Layout::~Layout()
{
}

ServerPtr Layout::getServer() 
{
    Config* config = getConfig();
    EQASSERT( config );
    return ( config ? config->getServer() : 0 );
}

}
}

#include "../fabric/layout.ipp"
template class eq::fabric::Layout< eq::admin::Config, eq::admin::Layout,
                                   eq::admin::View >;

/** @cond IGNORE */
template EQFABRIC_API std::ostream& eq::fabric::operator << ( std::ostream&,
                                                 const eq::admin::Super& );
/** @endcond */

