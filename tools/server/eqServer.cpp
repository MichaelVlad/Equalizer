
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

#include <eq/server/global.h>
#include <eq/server/init.h>
#include <eq/server/loader.h>
#include <eq/server/server.h>

#include <co/global.h>
#include <co/init.h>

#include <iostream>

#define CONFIG "server{ config{ appNode{ pipe {                            \
    window { viewport [ .25 .25 .5 .5 ] channel { name \"channel\" }}}}    \
    compound { channel \"channel\" wall { bottom_left  [ -.8 -.5 -1 ]      \
                                          bottom_right [  .8 -.5 -1 ]      \
                                          top_left     [ -.8  .5 -1 ] }}}}"

int main( const int argc, char** argv )
{
    if( !eq::server::init( argc, argv ))
        return EXIT_FAILURE;

    co::Global::setDefaultPort( EQ_DEFAULT_PORT );

    eq::server::Loader loader;
    eq::server::ServerPtr server;

    if( argc == 1 )
        server = loader.parseServer( CONFIG );
    else
        server = loader.loadFile( argv[1] );

    if( !server.isValid( ))
    {
        EQERROR << "Server load failed" << std::endl;
        return EXIT_FAILURE;
    }

    eq::server::Loader::addOutputCompounds( server );
    eq::server::Loader::addDestinationViews( server );
    eq::server::Loader::addDefaultObserver( server );
    eq::server::Loader::convertTo11( server );
    eq::server::Loader::convertTo12( server );

    if( server->getConnectionDescriptions().empty( )) // add default listener
    {
        EQINFO << "Adding default server connection" << std::endl;
        co::ConnectionDescriptionPtr connDesc = new co::ConnectionDescription;
        connDesc->port = co::Global::getDefaultPort();
        server->addConnectionDescription( connDesc );
    }

    if( !server->initLocal( argc, argv ))
    {
        EQERROR << "Can't create listener for server, please consult log" 
                << std::endl;
        return EXIT_FAILURE;
    }

    server->run();
    server->exitLocal();
    server->deleteConfigs();

    EQINFO << "Server ref count: " << server->getRefCount() << std::endl;

    return eq::server::exit() ? EXIT_SUCCESS : EXIT_FAILURE;
}

