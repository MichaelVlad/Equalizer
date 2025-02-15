
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

#ifndef EQSEQUEL_VIEWDATA_H
#define EQSEQUEL_VIEWDATA_H

#include <seq/api.h>
#include <seq/types.h>
#include <co/serializable.h>               // base class

namespace seq
{
    /** Stores per-view data. */
    class ViewData : public co::Serializable
    {
    public:
        /** Construct a new view data. @version 1.0 */
        SEQ_API ViewData();

        /** Destruct this view data. @version 1.0 */
        SEQ_API virtual ~ViewData();

        /** @name Operations */
        //@{
        /**
         * Handle the given event.
         *
         * The default implementation provides a pointer-based camera model and
         * some key event handling, all of which can be modified by overwriting
         * this method and handling the appropriate events.
         * @version 1.0
         */
        SEQ_API virtual bool handleEvent( const eq::ConfigEvent* event );

        /** Rotate the model matrix by the given increments. @version 1.0 */
        SEQ_API void spinModel( const float x, const float y, const float z );

        /** Move the model matrix by the given increments. @version 1.0 */
        SEQ_API void moveModel( const float x, const float y, const float z );

        /**
         * Enable or disable statistics rendering.
         *
         * The statistics are rendered in the views where they are enabled. The
         * default event handler of this view toggles the statistics rendering
         * state when 's' is pressed.
         *
         * @param on the state of the statistics rendering.
         * @version 1.0
         */
        SEQ_API void showStatistics( const bool on );

        /**
         * Update the view data.
         *
         * Called once at the end of each frame to trigger animations. The
         * default implementation updates the camera data.
         * @return true to request a redraw.
         * @version 1.0
         */
        virtual SEQ_API bool update();
        //@}

        /** @name Data Access. */
        //@{
        /** @return the current model matrix (global camera). @version 1.0 */
        const Matrix4f& getModelMatrix() const { return _modelMatrix; }

        /** @return true is statistics are rendered. @version 1.0 */
        bool getStatistics() const { return _statistics; }
        //@}

    protected:
        virtual void serialize( co::DataOStream& os, const uint64_t dirtyBits );
        virtual void deserialize( co::DataIStream& is,
                                  const uint64_t dirtyBits );

    private:
        /** The changed parts of the object since the last serialize(). */
        enum DirtyBits
        {
            DIRTY_MODELMATRIX = co::Serializable::DIRTY_CUSTOM << 0, // 1
            DIRTY_STATISTICS = co::Serializable::DIRTY_CUSTOM << 1 // 2
        };

        Matrix4f _modelMatrix;
        int32_t _spinX, _spinY;
        int32_t _advance;
        bool _statistics;
    };
}
#endif // EQSEQUEL_VIEWDATA_H
