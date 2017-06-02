// Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation; version 2 of the License.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, 51 Franklin
// Street, Suite 500, Boston, MA 02110-1335 USA.

/// @file
///
/// This file implements the overlaps functor and function.

#include "overlaps_functor.h"
#include "relops.h"

#include <memory>  // std::unique_ptr

#include <boost/geometry.hpp>

#include "dd/types/spatial_reference_system.h"  // dd::Spatial_reference_system
#include "gc_utils.h"
#include "geometries.h"
#include "geometries_traits.h"
#include "sql_exception_handler.h"  // handle_gis_exception

namespace bg = boost::geometry;

namespace gis {

/// Apply an Overlaps functor to two geometries, which both may be geometry
/// collections, and return the booelan result of the functor applied on each
/// combination of elements in the collections.
///
/// @tparam GC Coordinate specific gometry collection type.
///
/// @param f Functor to apply.
/// @param g1 First geometry.
/// @param g2 Second geometry.
///
/// @retval true g1 overlaps g2.
/// @retval false g1 doesn't overlap g2.
template <typename GC>
static bool geometry_collection_apply_overlaps(const Overlaps &f,
                                               const Geometry *g1,
                                               const Geometry *g2) {
  if (g1->type() == Geometry_type::kGeometrycollection &&
      g2->type() == Geometry_type::kGeometrycollection) {
    std::unique_ptr<Multipoint> g1_mpt;
    std::unique_ptr<Multilinestring> g1_mls;
    std::unique_ptr<Multipolygon> g1_mpy;
    std::unique_ptr<Multipoint> g2_mpt;
    std::unique_ptr<Multilinestring> g2_mls;
    std::unique_ptr<Multipolygon> g2_mpy;
    split_gc(down_cast<const Geometrycollection *>(g1), &g1_mpt, &g1_mls,
             &g1_mpy);
    gc_union(f.semi_major(), f.semi_minor(), &g1_mpt, &g1_mls, &g1_mpy);
    split_gc(down_cast<const Geometrycollection *>(g2), &g2_mpt, &g2_mls,
             &g2_mpy);
    gc_union(f.semi_major(), f.semi_minor(), &g2_mpt, &g2_mls, &g2_mpy);

    int g1_dim;
    if (!g1_mpy->empty())
      g1_dim = 2;
    else if (!g1_mls->empty())
      g1_dim = 1;
    else if (!g1_mpt->empty())
      g1_dim = 0;
    else {
      DBUG_ASSERT(false); /* purecov: inspected */
      g1_dim = -1;        /* purecov: inspected */
    }

    int g2_dim;
    if (!g2_mpy->empty())
      g2_dim = 2;
    else if (!g2_mls->empty())
      g2_dim = 1;
    else if (!g2_mpt->empty())
      g2_dim = 0;
    else {
      DBUG_ASSERT(false); /* purecov: inspected */
      g2_dim = -1;        /* purecov: inspected */
    }

    if (g1_dim != g2_dim) throw null_value_exception();

    switch (g1_dim) {
      case 0:
        return f(g1_mpt.get(), g2_mpt.get());
        break;
      case 1:
        return f(g1_mpt.get(), g2_mpt.get()) || f(g1_mls.get(), g2_mls.get());
        break;
      case 2:
        return f(g1_mpt.get(), g2_mpt.get()) || f(g1_mls.get(), g2_mls.get()) ||
               f(g1_mpy.get(), g2_mpy.get());
        break;
      default:
        DBUG_ASSERT(false);           /* purecov: inspected */
        throw null_value_exception(); /* purecov: inspected */
        break;
    }
  } else if (g1->type() == Geometry_type::kGeometrycollection) {
    return f(g2, g1);
  } else if (g2->type() == Geometry_type::kGeometrycollection) {
    std::unique_ptr<Multipoint> g2_mpt;
    std::unique_ptr<Multilinestring> g2_mls;
    std::unique_ptr<Multipolygon> g2_mpy;
    split_gc(down_cast<const Geometrycollection *>(g2), &g2_mpt, &g2_mls,
             &g2_mpy);
    gc_union(f.semi_major(), f.semi_minor(), &g2_mpt, &g2_mls, &g2_mpy);

    int g2_dim;
    if (!g2_mpy->empty())
      g2_dim = 2;
    else if (!g2_mls->empty())
      g2_dim = 1;
    else if (!g2_mpt->empty())
      g2_dim = 0;
    else {
      DBUG_ASSERT(false); /* purecov: inspected */
      g2_dim = -1;        /* purecov: inspected */
    }

    switch (g1->type()) {
      case Geometry_type::kPoint:
      case Geometry_type::kMultipoint:
        if (g2_dim != 0) throw null_value_exception();
        return f(g1, g2_mpt.get());
      case Geometry_type::kLinestring:
      case Geometry_type::kMultilinestring:
        if (g2_dim != 1) throw null_value_exception();
        return f(g1, g2_mls.get());
      case Geometry_type::kPolygon:
      case Geometry_type::kMultipolygon:
        if (g2_dim != 2) throw null_value_exception();
        return f(g1, g2_mpy.get());
      default:
        // All possible combinations should be covered above.
        DBUG_ASSERT(false); /* purecov: inspected */
        return false;
    }
  } else {
    DBUG_ASSERT(false); /* purecov: inspected */
    return f(g1, g2);
  }
}

Overlaps::Overlaps(double semi_major, double semi_minor)
    : m_semi_major(semi_major),
      m_semi_minor(semi_minor),
      m_geographic_ll_aa_strategy(
          bg::srs::spheroid<double>(semi_major, semi_minor)) {}

bool Overlaps::operator()(const Geometry *g1, const Geometry *g2) const {
  return apply(*this, g1, g2);
}

bool Overlaps::eval(const Geometry *g1, const Geometry *g2) const {
  // All parameter type combinations have been implemented.
  DBUG_ASSERT(false);
  throw not_implemented_exception(g1->coordinate_system(), g1->type(),
                                  g2->type());
}

//////////////////////////////////////////////////////////////////////////////

// overlaps(Cartesian_point, *)

bool Overlaps::eval(const Cartesian_point *g1,
                    const Cartesian_point *g2) const {
  // The interior of a point can never be within both the interior and exterior
  // of another geometry.
  return false;
}

bool Overlaps::eval(const Cartesian_point *g1,
                    const Cartesian_linestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_point *g1,
                    const Cartesian_polygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_point *g1,
                    const Cartesian_geometrycollection *g2) const {
  return geometry_collection_apply_overlaps<Cartesian_geometrycollection>(
      *this, g1, g2);
}

bool Overlaps::eval(const Cartesian_point *g1,
                    const Cartesian_multipoint *g2) const {
  // The interior of a point can never be within both the interior and exterior
  // of another geometry.
  return false;
}

bool Overlaps::eval(const Cartesian_point *g1,
                    const Cartesian_multilinestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_point *g1,
                    const Cartesian_multipolygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

//////////////////////////////////////////////////////////////////////////////

// overlaps(Cartesian_linestring, *)

bool Overlaps::eval(const Cartesian_linestring *g1,
                    const Cartesian_point *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_linestring *g1,
                    const Cartesian_linestring *g2) const {
  return bg::overlaps(*g1, *g2);
}

bool Overlaps::eval(const Cartesian_linestring *g1,
                    const Cartesian_polygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_linestring *g1,
                    const Cartesian_geometrycollection *g2) const {
  return geometry_collection_apply_overlaps<Cartesian_geometrycollection>(
      *this, g1, g2);
}

bool Overlaps::eval(const Cartesian_linestring *g1,
                    const Cartesian_multipoint *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_linestring *g1,
                    const Cartesian_multilinestring *g2) const {
  return bg::overlaps(*g1, *g2);
}

bool Overlaps::eval(const Cartesian_linestring *g1,
                    const Cartesian_multipolygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

//////////////////////////////////////////////////////////////////////////////

// overlaps(Cartesian_polygon, *)

bool Overlaps::eval(const Cartesian_polygon *g1,
                    const Cartesian_point *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_polygon *g1,
                    const Cartesian_linestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_polygon *g1,
                    const Cartesian_polygon *g2) const {
  return bg::overlaps(*g1, *g2);
}

bool Overlaps::eval(const Cartesian_polygon *g1,
                    const Cartesian_geometrycollection *g2) const {
  return geometry_collection_apply_overlaps<Cartesian_geometrycollection>(
      *this, g1, g2);
}

bool Overlaps::eval(const Cartesian_polygon *g1,
                    const Cartesian_multipoint *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_polygon *g1,
                    const Cartesian_multilinestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_polygon *g1,
                    const Cartesian_multipolygon *g2) const {
  return bg::overlaps(*g1, *g2);
}

//////////////////////////////////////////////////////////////////////////////

// overlaps(Cartesian_geometrycollection, *)

bool Overlaps::eval(const Cartesian_geometrycollection *g1,
                    const Geometry *g2) const {
  return geometry_collection_apply_overlaps<Cartesian_geometrycollection>(
      *this, g1, g2);
}

//////////////////////////////////////////////////////////////////////////////

// overlaps(Cartesian_multipoint, *)

bool Overlaps::eval(const Cartesian_multipoint *g1,
                    const Cartesian_point *g2) const {
  // The interior of a point can never be within both the interior and exterior
  // of another geometry.
  return false;
}

bool Overlaps::eval(const Cartesian_multipoint *g1,
                    const Cartesian_linestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_multipoint *g1,
                    const Cartesian_polygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_multipoint *g1,
                    const Cartesian_geometrycollection *g2) const {
  return geometry_collection_apply_overlaps<Cartesian_geometrycollection>(
      *this, g1, g2);
}

bool Overlaps::eval(const Cartesian_multipoint *g1,
                    const Cartesian_multipoint *g2) const {
  return bg::overlaps(*g1, *g2);
}

bool Overlaps::eval(const Cartesian_multipoint *g1,
                    const Cartesian_multilinestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_multipoint *g1,
                    const Cartesian_multipolygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

//////////////////////////////////////////////////////////////////////////////

// overlaps(Cartesian_multilinestring, *)

bool Overlaps::eval(const Cartesian_multilinestring *g1,
                    const Cartesian_point *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_multilinestring *g1,
                    const Cartesian_linestring *g2) const {
  return bg::overlaps(*g1, *g2);
}

bool Overlaps::eval(const Cartesian_multilinestring *g1,
                    const Cartesian_polygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_multilinestring *g1,
                    const Cartesian_geometrycollection *g2) const {
  return geometry_collection_apply_overlaps<Cartesian_geometrycollection>(
      *this, g1, g2);
}

bool Overlaps::eval(const Cartesian_multilinestring *g1,
                    const Cartesian_multipoint *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_multilinestring *g1,
                    const Cartesian_multilinestring *g2) const {
  return bg::overlaps(*g1, *g2);
}

bool Overlaps::eval(const Cartesian_multilinestring *g1,
                    const Cartesian_multipolygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

//////////////////////////////////////////////////////////////////////////////

// overlaps(Cartesian_multipolygon, *)

bool Overlaps::eval(const Cartesian_multipolygon *g1,
                    const Cartesian_point *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_multipolygon *g1,
                    const Cartesian_linestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_multipolygon *g1,
                    const Cartesian_polygon *g2) const {
  return bg::overlaps(*g1, *g2);
}

bool Overlaps::eval(const Cartesian_multipolygon *g1,
                    const Cartesian_geometrycollection *g2) const {
  return geometry_collection_apply_overlaps<Cartesian_geometrycollection>(
      *this, g1, g2);
}

bool Overlaps::eval(const Cartesian_multipolygon *g1,
                    const Cartesian_multipoint *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_multipolygon *g1,
                    const Cartesian_multilinestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Cartesian_multipolygon *g1,
                    const Cartesian_multipolygon *g2) const {
  return bg::overlaps(*g1, *g2);
}

//////////////////////////////////////////////////////////////////////////////

// overlaps(Geographic_point, *)

bool Overlaps::eval(const Geographic_point *g1,
                    const Geographic_point *g2) const {
  // The interior of a point can never be within both the interior and exterior
  // of another geometry.
  return false;
}

bool Overlaps::eval(const Geographic_point *g1,
                    const Geographic_linestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_point *g1,
                    const Geographic_polygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_point *g1,
                    const Geographic_geometrycollection *g2) const {
  return geometry_collection_apply_overlaps<Geographic_geometrycollection>(
      *this, g1, g2);
}

bool Overlaps::eval(const Geographic_point *g1,
                    const Geographic_multipoint *g2) const {
  // The interior of a point can never be within both the interior and exterior
  // of another geometry.
  return false;
}

bool Overlaps::eval(const Geographic_point *g1,
                    const Geographic_multilinestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_point *g1,
                    const Geographic_multipolygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

//////////////////////////////////////////////////////////////////////////////

// overlaps(Geographic_linestring, *)

bool Overlaps::eval(const Geographic_linestring *g1,
                    const Geographic_point *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_linestring *g1,
                    const Geographic_linestring *g2) const {
  return bg::overlaps(*g1, *g2, m_geographic_ll_aa_strategy);
}

bool Overlaps::eval(const Geographic_linestring *g1,
                    const Geographic_polygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_linestring *g1,
                    const Geographic_geometrycollection *g2) const {
  return geometry_collection_apply_overlaps<Geographic_geometrycollection>(
      *this, g1, g2);
}

bool Overlaps::eval(const Geographic_linestring *g1,
                    const Geographic_multipoint *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_linestring *g1,
                    const Geographic_multilinestring *g2) const {
  return bg::overlaps(*g1, *g2, m_geographic_ll_aa_strategy);
}

bool Overlaps::eval(const Geographic_linestring *g1,
                    const Geographic_multipolygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

//////////////////////////////////////////////////////////////////////////////

// overlaps(Geographic_polygon, *)

bool Overlaps::eval(const Geographic_polygon *g1,
                    const Geographic_point *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_polygon *g1,
                    const Geographic_linestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_polygon *g1,
                    const Geographic_polygon *g2) const {
  return bg::overlaps(*g1, *g2, m_geographic_ll_aa_strategy);
}

bool Overlaps::eval(const Geographic_polygon *g1,
                    const Geographic_geometrycollection *g2) const {
  return geometry_collection_apply_overlaps<Geographic_geometrycollection>(
      *this, g1, g2);
}

bool Overlaps::eval(const Geographic_polygon *g1,
                    const Geographic_multipoint *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_polygon *g1,
                    const Geographic_multilinestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_polygon *g1,
                    const Geographic_multipolygon *g2) const {
  return bg::overlaps(*g1, *g2, m_geographic_ll_aa_strategy);
}

//////////////////////////////////////////////////////////////////////////////

// overlaps(Geographic_geometrycollection, *)

bool Overlaps::eval(const Geographic_geometrycollection *g1,
                    const Geometry *g2) const {
  return geometry_collection_apply_overlaps<Geographic_geometrycollection>(
      *this, g1, g2);
}

//////////////////////////////////////////////////////////////////////////////

// overlaps(Geographic_multipoint, *)

bool Overlaps::eval(const Geographic_multipoint *g1,
                    const Geographic_point *g2) const {
  // The interior of a point can never be within both the interior and exterior
  // of another geometry.
  return false;
}

bool Overlaps::eval(const Geographic_multipoint *g1,
                    const Geographic_linestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_multipoint *g1,
                    const Geographic_polygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_multipoint *g1,
                    const Geographic_geometrycollection *g2) const {
  return geometry_collection_apply_overlaps<Geographic_geometrycollection>(
      *this, g1, g2);
}

bool Overlaps::eval(const Geographic_multipoint *g1,
                    const Geographic_multipoint *g2) const {
  // Default strategy is OK. P/P computations do not depend on shape of
  // ellipsoid.
  return bg::overlaps(*g1, *g2);
}

bool Overlaps::eval(const Geographic_multipoint *g1,
                    const Geographic_multilinestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_multipoint *g1,
                    const Geographic_multipolygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

//////////////////////////////////////////////////////////////////////////////

// overlaps(Geographic_multilinestring, *)

bool Overlaps::eval(const Geographic_multilinestring *g1,
                    const Geographic_point *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_multilinestring *g1,
                    const Geographic_linestring *g2) const {
  return bg::overlaps(*g1, *g2, m_geographic_ll_aa_strategy);
}

bool Overlaps::eval(const Geographic_multilinestring *g1,
                    const Geographic_polygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_multilinestring *g1,
                    const Geographic_geometrycollection *g2) const {
  return geometry_collection_apply_overlaps<Geographic_geometrycollection>(
      *this, g1, g2);
}

bool Overlaps::eval(const Geographic_multilinestring *g1,
                    const Geographic_multipoint *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_multilinestring *g1,
                    const Geographic_multilinestring *g2) const {
  return bg::overlaps(*g1, *g2, m_geographic_ll_aa_strategy);
}

bool Overlaps::eval(const Geographic_multilinestring *g1,
                    const Geographic_multipolygon *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

//////////////////////////////////////////////////////////////////////////////

// overlaps(Geographic_multipolygon, *)

bool Overlaps::eval(const Geographic_multipolygon *g1,
                    const Geographic_point *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_multipolygon *g1,
                    const Geographic_linestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_multipolygon *g1,
                    const Geographic_polygon *g2) const {
  return bg::overlaps(*g1, *g2, m_geographic_ll_aa_strategy);
}

bool Overlaps::eval(const Geographic_multipolygon *g1,
                    const Geographic_geometrycollection *g2) const {
  return geometry_collection_apply_overlaps<Geographic_geometrycollection>(
      *this, g1, g2);
}

bool Overlaps::eval(const Geographic_multipolygon *g1,
                    const Geographic_multipoint *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_multipolygon *g1,
                    const Geographic_multilinestring *g2) const {
  // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
  throw null_value_exception();
}

bool Overlaps::eval(const Geographic_multipolygon *g1,
                    const Geographic_multipolygon *g2) const {
  return bg::overlaps(*g1, *g2, m_geographic_ll_aa_strategy);
}

//////////////////////////////////////////////////////////////////////////////

bool overlaps(const dd::Spatial_reference_system *srs, const Geometry *g1,
              const Geometry *g2, const char *func_name, bool *overlaps,
              bool *null) noexcept {
  try {
    DBUG_ASSERT(g1->coordinate_system() == g2->coordinate_system());
    DBUG_ASSERT(srs == nullptr ||
                ((srs->is_cartesian() &&
                  g1->coordinate_system() == Coordinate_system::kCartesian) ||
                 (srs->is_geographic() &&
                  g1->coordinate_system() == Coordinate_system::kGeographic)));

    if ((*null = (g1->is_empty() || g2->is_empty()))) return false;

    Overlaps overlaps_func(srs ? srs->semi_major_axis() : 0.0,
                           srs ? srs->semi_minor_axis() : 0.0);
    *overlaps = overlaps_func(g1, g2);
  } catch (const null_value_exception &) {
    *null = true;
    return false;
  } catch (...) {
    handle_gis_exception(func_name);
    return true;
  }

  return false;
}

}  // namespace gis