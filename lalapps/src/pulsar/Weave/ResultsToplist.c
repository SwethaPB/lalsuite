//
// Copyright (C) 2016, 2017 Karl Wette
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with with program; see the file COPYING. If not, write to the
// Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
// MA 02111-1307 USA
//

///
/// \file
/// \ingroup lalapps_pulsar_Weave
///

#include "ResultsToplist.h"

///
/// Internal definition of toplist of output results
///
struct tagWeaveResultsToplist {
  /// Number of spindown parameters to output
  size_t nspins;
  /// If outputting per-detector quantities, list of detectors
  LALStringVector *per_detectors;
  /// Number of per-segment items being output (may be zero)
  UINT4 per_nsegments;
  /// Name of ranking statistic
  const char *stat_name;
  /// Description of ranking statistic
  const char *stat_desc;
  /// Function which minimally initialises a toplist item before it is added
  WeaveResultsToplistItemInit toplist_item_init_fcn;
  /// Heap which ranks output results by a particular statistic
  LALHeap *heap;
  /// Save a no-longer-used toplist item for re-use
  WeaveResultsToplistItem *saved_item;
};

///
/// \name Internal routines
///
/// @{

static WeaveResultsToplistItem *toplist_item_create( const WeaveResultsToplist *toplist );
static int compare_templates( BOOLEAN *equal, const char *loc_str, const char *tmpl_str, const REAL8 param_tol_mism, const WeavePhysicalToLattice phys_to_latt, const gsl_matrix *metric, const void *transf_data, const PulsarDopplerParams *phys_1, const PulsarDopplerParams *phys_2 );
static int compare_vectors( BOOLEAN *equal, const VectorComparison *result_tol, const REAL4Vector *res_1, const REAL4Vector *res_2 );
static int toplist_fits_table_init( FITSFile *file, const WeaveResultsToplist *toplist );
static int toplist_fits_table_write_visitor( void *param, const void *x );
static int toplist_item_sort_by_semi_phys( const void *x, const void *y );
static void toplist_item_destroy( WeaveResultsToplistItem *item );

/// @}

///
/// Create a toplist item
///
WeaveResultsToplistItem *toplist_item_create(
  const WeaveResultsToplist *toplist
  )
{

  // Check input
  XLAL_CHECK_NULL( toplist != NULL, XLAL_EFAULT );

  // Allocate memory for item
  WeaveResultsToplistItem *item = XLALCalloc( 1, sizeof( *item ) );
  XLAL_CHECK_NULL( item != NULL, XLAL_ENOMEM );

  // Allocate memory for per-segment output results
  if ( toplist->per_nsegments > 0 ) {
    item->coh_alpha = XLALCalloc( toplist->per_nsegments, sizeof( *item->coh_alpha ) );
    XLAL_CHECK_NULL( item->coh_alpha != NULL, XLAL_ENOMEM );
    item->coh_delta = XLALCalloc( toplist->per_nsegments, sizeof( *item->coh_delta ) );
    XLAL_CHECK_NULL( item->coh_delta != NULL, XLAL_ENOMEM );
    for ( size_t k = 0; k <= toplist->nspins; ++k ) {
      item->coh_fkdot[k] = XLALCalloc( toplist->per_nsegments, sizeof( *item->coh_fkdot[k] ) );
      XLAL_CHECK_NULL( item->coh_fkdot[k] != NULL, XLAL_ENOMEM );
    }
    item->coh2F = XLALCalloc( toplist->per_nsegments, sizeof( *item->coh2F ) );
    XLAL_CHECK_NULL( item->coh2F != NULL, XLAL_ENOMEM );
  }

  // Allocate memory for per-detector and per-segment output results
  if ( toplist->per_detectors != NULL && toplist->per_nsegments > 0 ) {
    for ( size_t i = 0; i < toplist->per_detectors->length; ++i ) {
      item->coh2F_det[i] = XLALCalloc( toplist->per_nsegments, sizeof( *item->coh2F_det[i] ) );
      XLAL_CHECK_NULL( item->coh2F_det[i] != NULL, XLAL_ENOMEM );
    }
  }

  return item;

}

///
/// Destroy a toplist item
///
void toplist_item_destroy(
  WeaveResultsToplistItem *item
  )
{
  if ( item != NULL ) {
    XLALFree( item->coh_alpha );
    XLALFree( item->coh_delta );
    for ( size_t k = 0; k < PULSAR_MAX_SPINS; ++k ) {
      XLALFree( item->coh_fkdot[k] );
    }
    XLALFree( item->coh2F );
    for ( size_t i = 0; i < PULSAR_MAX_DETECTORS; ++i ) {
      XLALFree( item->coh2F_det[i] );
    }
    XLALFree( item );
  }
}

///
/// Initialise a FITS table for writing/reading a toplist
///
int toplist_fits_table_init(
  FITSFile *file,
  const WeaveResultsToplist *toplist
  )
{

  // Check input
  XLAL_CHECK( file != NULL, XLAL_EFAULT );

  char col_name[64];

  // Begin FITS table description
  XLAL_FITS_TABLE_COLUMN_BEGIN( WeaveResultsToplistItem );

  // Add columns for semicoherent template parameters
  XLAL_CHECK( XLAL_FITS_TABLE_COLUMN_ADD_NAMED( file, REAL8, semi_alpha, "alpha [rad]" ) == XLAL_SUCCESS, XLAL_EFUNC );
  XLAL_CHECK( XLAL_FITS_TABLE_COLUMN_ADD_NAMED( file, REAL8, semi_delta, "delta [rad]" ) == XLAL_SUCCESS, XLAL_EFUNC );
  XLAL_CHECK( XLAL_FITS_TABLE_COLUMN_ADD_NAMED( file, REAL8, semi_fkdot[0], "freq [Hz]" ) == XLAL_SUCCESS, XLAL_EFUNC );
  for ( size_t k = 1; k <= toplist->nspins; ++k ) {
    snprintf( col_name, sizeof( col_name ), "f%zudot [Hz/s^%zu]", k, k );
    XLAL_CHECK( XLAL_FITS_TABLE_COLUMN_ADD_NAMED( file, REAL8, semi_fkdot[k], col_name ) == XLAL_SUCCESS, XLAL_EFUNC );
  }

  // Add columns for mean multi- and per-detector F-statistic
  XLAL_CHECK( XLAL_FITS_TABLE_COLUMN_ADD( file, REAL4, mean2F ) == XLAL_SUCCESS, XLAL_EFUNC );
  if ( toplist->per_detectors != NULL ) {
    for ( size_t i = 0; i < toplist->per_detectors->length; ++i ) {
      snprintf( col_name, sizeof( col_name ), "mean2F_%s", toplist->per_detectors->data[i] );
      XLAL_CHECK( XLAL_FITS_TABLE_COLUMN_ADD_NAMED( file, REAL4, mean2F_det[i], col_name ) == XLAL_SUCCESS, XLAL_EFUNC );
    }
  }

  if ( toplist->per_nsegments > 0 ) {

    // Add columns for coherent template parameters
    XLAL_CHECK( XLAL_FITS_TABLE_COLUMN_ADD_PTR_ARRAY_NAMED( file, REAL8, toplist->per_nsegments, coh_alpha, "alpha_seg [rad]" ) == XLAL_SUCCESS, XLAL_EFUNC );
    XLAL_CHECK( XLAL_FITS_TABLE_COLUMN_ADD_PTR_ARRAY_NAMED( file, REAL8, toplist->per_nsegments, coh_delta, "delta_seg [rad]" ) == XLAL_SUCCESS, XLAL_EFUNC );
    XLAL_CHECK( XLAL_FITS_TABLE_COLUMN_ADD_PTR_ARRAY_NAMED( file, REAL8, toplist->per_nsegments, coh_fkdot[0], "freq_seg [Hz]" ) == XLAL_SUCCESS, XLAL_EFUNC );
    for ( size_t k = 1; k <= toplist->nspins; ++k ) {
      snprintf( col_name, sizeof( col_name ), "f%zudot_seg [Hz/s^%zu]", k, k );
      XLAL_CHECK( XLAL_FITS_TABLE_COLUMN_ADD_PTR_ARRAY_NAMED( file, REAL8, toplist->per_nsegments, coh_fkdot[k], col_name ) == XLAL_SUCCESS, XLAL_EFUNC );
    }

    // Add columns for coherent multi- and per-detector F-statistic
    XLAL_CHECK( XLAL_FITS_TABLE_COLUMN_ADD_PTR_ARRAY_NAMED( file, REAL4, toplist->per_nsegments, coh2F, "coh2F_seg" ) == XLAL_SUCCESS, XLAL_EFUNC );
    if ( toplist->per_detectors != NULL ) {
      for ( size_t i = 0; i < toplist->per_detectors->length; ++i ) {
        snprintf( col_name, sizeof( col_name ), "coh2F_%s_seg", toplist->per_detectors->data[i] );
        XLAL_CHECK( XLAL_FITS_TABLE_COLUMN_ADD_PTR_ARRAY_NAMED( file, REAL4, toplist->per_nsegments, coh2F_det[i], col_name ) == XLAL_SUCCESS, XLAL_EFUNC );
      }
    }

  }

  return XLAL_SUCCESS;

}

///
/// Visitor function for writing a toplist to a FITS table
///
int toplist_fits_table_write_visitor(
  void *param,
  const void *x
  )
{
  FITSFile *file = ( FITSFile * ) param;
  XLAL_CHECK( XLALFITSTableWriteRow( file, x ) == XLAL_SUCCESS, XLAL_EFUNC );
  return XLAL_SUCCESS;
}

///
/// Sort toplist items by physical coordinates of semicoherent template.
///
/// For stable comparisons, the order of parameter comparisons should be the same
/// as the order in which parameters are generated by the search lattice tiling.
///
int toplist_item_sort_by_semi_phys(
  const void *x,
  const void *y
  )
{
  const WeaveResultsToplistItem *ix = *( const WeaveResultsToplistItem *const * ) x;
  const WeaveResultsToplistItem *iy = *( const WeaveResultsToplistItem *const * ) y;
  WEAVE_COMPARE_BY( ix->semi_alpha, iy->semi_alpha );   // Compare in ascending order
  WEAVE_COMPARE_BY( ix->semi_delta, iy->semi_delta );   // Compare in ascending order
  for ( size_t s = 1; s < XLAL_NUM_ELEM( ix->semi_fkdot ); ++s ) {
    WEAVE_COMPARE_BY( ix->semi_fkdot[s], iy->semi_fkdot[s] );   // Compare in ascending order
  }
  WEAVE_COMPARE_BY( ix->semi_fkdot[0], iy->semi_fkdot[0] );   // Compare in ascending order
  return 0;
}

///
/// Compute two template parameters
///
int compare_templates(
  BOOLEAN *equal,
  const char *loc_str,
  const char *tmpl_str,
  const REAL8 param_tol_mism,
  const WeavePhysicalToLattice phys_to_latt,
  const gsl_matrix *metric,
  const void *transf_data,
  const PulsarDopplerParams *phys_1,
  const PulsarDopplerParams *phys_2
  )
{

  // Check input
  XLAL_CHECK( equal != NULL, XLAL_EINVAL );
  XLAL_CHECK( loc_str != NULL, XLAL_EINVAL );
  XLAL_CHECK( tmpl_str != NULL, XLAL_EINVAL );
  XLAL_CHECK( param_tol_mism > 0, XLAL_EINVAL );
  XLAL_CHECK( phys_to_latt != NULL, XLAL_EFAULT );
  XLAL_CHECK( metric != NULL, XLAL_EFAULT );
  XLAL_CHECK( transf_data != NULL, XLAL_EFAULT );
  XLAL_CHECK( phys_1 != NULL, XLAL_EFAULT );
  XLAL_CHECK( phys_2 != NULL, XLAL_EFAULT );

  // Transform physical point to lattice coordinates
  double latt_1_array[metric->size1];
  gsl_vector_view latt_1_view = gsl_vector_view_array( latt_1_array, metric->size1 );
  gsl_vector *const latt_1 = &latt_1_view.vector;
  XLAL_CHECK( ( phys_to_latt )( latt_1, phys_1, transf_data ) == XLAL_SUCCESS, XLAL_EFUNC );
  double latt_2_array[metric->size1];
  gsl_vector_view latt_2_view = gsl_vector_view_array( latt_2_array, metric->size1 );
  gsl_vector *const latt_2 = &latt_2_view.vector;
  XLAL_CHECK( ( phys_to_latt )( latt_2, phys_2, transf_data ) == XLAL_SUCCESS, XLAL_EFUNC );

  // Store difference between lattice coordinates in 'u'
  double u_array[metric->size1];
  gsl_vector_view u_view = gsl_vector_view_array( u_array, metric->size1 );
  gsl_vector *const u = &u_view.vector;
  gsl_vector_memcpy( u, latt_1 );
  gsl_vector_sub( u, latt_2 );

  // Multiply 'u' by metric, storing result in 'v'
  double v_array[metric->size1];
  gsl_vector_view v_view = gsl_vector_view_array( v_array, metric->size1 );
  gsl_vector *const v = &v_view.vector;
  gsl_blas_dsymv( CblasUpper, 1.0, metric, u, 0.0, v );

  // Compute mismatch and compare to tolerance
  REAL8 mism = 0;
  gsl_blas_ddot( u, v, &mism );

  // If mismatch is above tolerance, print error message
  if ( mism > param_tol_mism ) {
    *equal = 0;
    XLALPrintInfo( "%s: at %s, mismatch between %s template parameters exceeds tolerance: %g > %g\n", __func__, loc_str, tmpl_str, mism, param_tol_mism );
    const PulsarDopplerParams *phys[2] = { phys_1, phys_2 };
    gsl_vector *latt[2] = { latt_1, latt_2 };
    for ( size_t i = 0; i < 2; ++i ) {
      XLALPrintInfo( "%s:     physical %zu = {%.15g,%.15g,%.15g,%.15g}\n", __func__, i, phys[i]->Alpha, phys[i]->Delta, phys[i]->fkdot[0], phys[i]->fkdot[1] );
    }
    for ( size_t i = 0; i < 2; ++i ) {
      XLALPrintInfo( "%s:     lattice %zu = ", __func__, i );
      for ( size_t j = 0; j < latt[i]->size; ++j ) {
        XLALPrintInfo( "%c%.15g", j == 0 ? '{' : ',', gsl_vector_get( latt[i], j ) );
      }
      XLALPrintInfo( "}\n" );
    }
    XLALPrintInfo( "%s:     lattice diff = ", __func__ );
    for ( size_t j = 0; j < u->size; ++j ) {
      XLALPrintInfo( "%c%.15g", j == 0 ? '{' : ',', gsl_vector_get( u, j ) );
    }
    XLALPrintInfo( "}\n" );
    XLALPrintInfo( "%s:     metric dot = ", __func__ );
    for ( size_t j = 0; j < u->size; ++j ) {
      XLALPrintInfo( "%c%.15g", j == 0 ? '{' : ',', gsl_vector_get( u, j ) * gsl_vector_get( v, j ) );
    }
    XLALPrintInfo( "}\n" );
  }

  return XLAL_SUCCESS;

}

///
/// Compare two vectors of results
///
int compare_vectors(
  BOOLEAN *equal,
  const VectorComparison *result_tol,
  const REAL4Vector *res_1,
  const REAL4Vector *res_2
  )
{
  VectorComparison XLAL_INIT_DECL( result_diff );
  int errnum = 0;
  XLAL_TRY( XLALCompareREAL4Vectors( &result_diff, res_1, res_2, result_tol ), errnum );
  if ( errnum == XLAL_ETOL ) {
    *equal = 0;
  } else if ( errnum != 0 ) {
    XLAL_ERROR( XLAL_EFUNC );
  }
  return XLAL_SUCCESS;
}

///
/// Create results toplist
///
WeaveResultsToplist *XLALWeaveResultsToplistCreate(
  const size_t nspins,
  const LALStringVector *per_detectors,
  const UINT4 per_nsegments,
  const char *stat_name,
  const char *stat_desc,
  const int toplist_limit,
  WeaveResultsToplistItemInit toplist_item_init_fcn,
  LALHeapCmpFcn toplist_item_compare_fcn
  )
{

  // Check input
  XLAL_CHECK_NULL( stat_name != NULL, XLAL_EFAULT );
  XLAL_CHECK_NULL( stat_desc != NULL, XLAL_EFAULT );
  XLAL_CHECK_NULL( toplist_limit >= 0, XLAL_EINVAL );

  // Allocate memory
  WeaveResultsToplist *toplist = XLALCalloc( 1, sizeof( *toplist ) );
  XLAL_CHECK_NULL( toplist != NULL, XLAL_ENOMEM );

  // Set fields
  toplist->nspins = nspins;
  toplist->per_nsegments = per_nsegments;
  toplist->stat_name = stat_name;
  toplist->stat_desc = stat_desc;
  toplist->toplist_item_init_fcn = toplist_item_init_fcn;

  // Copy list of detectors
  if ( per_detectors != NULL ) {
    toplist->per_detectors = XLALCopyStringVector( per_detectors );
    XLAL_CHECK_NULL( toplist->per_detectors != NULL, XLAL_EFUNC );
  }

  // Create heap which ranks output results using the given comparison function
  toplist->heap = XLALHeapCreate( ( LALHeapDtorFcn ) toplist_item_destroy, toplist_limit, +1, toplist_item_compare_fcn );
  XLAL_CHECK_NULL( toplist->heap != NULL, XLAL_EFUNC );

  return toplist;

}

///
/// Free results toplist
///
void XLALWeaveResultsToplistDestroy(
  WeaveResultsToplist *toplist
  )
{
  if ( toplist != NULL ) {
    XLALDestroyStringVector( toplist->per_detectors );
    XLALHeapDestroy( toplist->heap );
    toplist_item_destroy( toplist->saved_item );
    XLALFree( toplist );
  }
}

///
/// Add semicoherent results to toplist
///
int XLALWeaveResultsToplistAdd(
  WeaveResultsToplist *toplist,
  const WeaveSemiResults *semi_res,
  const UINT4 semi_nfreqs
  )
{

  // Check input
  XLAL_CHECK( toplist != NULL, XLAL_EFAULT );
  XLAL_CHECK( semi_res != NULL, XLAL_EFAULT );

  // Iterate over the frequency bins of the semicoherent results
  for ( size_t freq_idx = 0; freq_idx < semi_nfreqs; ++freq_idx ) {

    // Create a new toplist item if needed
    if ( toplist->saved_item == NULL ) {
      toplist->saved_item = toplist_item_create( toplist );
      XLAL_CHECK( toplist->saved_item != NULL, XLAL_ENOMEM );
    }
    WeaveResultsToplistItem *item = toplist->saved_item;

    // Perform minimal initialisation of toplist item
    ( toplist->toplist_item_init_fcn )( item, semi_res, freq_idx );

    // Add item to heap
    XLAL_CHECK( XLALHeapAdd( toplist->heap, ( void ** ) &toplist->saved_item ) == XLAL_SUCCESS, XLAL_EFUNC );

    // Skip remainder of loop if item was not added to heap
    if ( item == toplist->saved_item ) {
      continue;
    }

    // Set all semicoherent template parameters
    item->semi_alpha = semi_res->semi_phys.Alpha;
    item->semi_delta = semi_res->semi_phys.Delta;
    for ( size_t k = 0; k <= toplist->nspins; ++k ) {
      item->semi_fkdot[k] = semi_res->semi_phys.fkdot[k];
    }

    // Set all coherent template parameters
    if ( toplist->per_nsegments > 0 ) {
      for ( size_t j = 0; j < semi_res->nsegments; ++j ) {
        item->coh_alpha[j] = semi_res->coh_phys[j].Alpha;
        item->coh_delta[j] = semi_res->coh_phys[j].Delta;
        for ( size_t k = 0; k <= toplist->nspins; ++k ) {
          item->coh_fkdot[k][j] = semi_res->coh_phys[j].fkdot[k];
        }
      }
    }

    // Update semicoherent and coherent template frequency
    item->semi_fkdot[0] = semi_res->semi_phys.fkdot[0] + freq_idx * semi_res->dfreq;
    if ( toplist->per_nsegments > 0 ) {
      for ( size_t j = 0; j < semi_res->nsegments; ++j ) {
        item->coh_fkdot[0][j] = semi_res->coh_phys[j].fkdot[0] + freq_idx * semi_res->dfreq;
      }
    }

    // Skip remainder of loop if simulating search
    if ( semi_res->simulation_level & WEAVE_SIMULATE ) {
      continue;
    }

    // Update multi-detector F-statistics
    item->mean2F = semi_res->mean2F->data[freq_idx];
    if ( toplist->per_nsegments > 0 ) {
      for ( size_t j = 0; j < semi_res->nsegments; ++j ) {
        item->coh2F[j] = semi_res->coh2F[j][freq_idx];
      }
    }

    // Update per-detector F-statistics
    if ( toplist->per_detectors != NULL ) {
      for ( size_t i = 0; i < semi_res->ndetectors; ++i ) {
        item->mean2F_det[i] = semi_res->mean2F_det[i]->data[freq_idx];
        if (  toplist->per_nsegments > 0 ) {
          for ( size_t j = 0; j < semi_res->nsegments; ++j ) {
            if ( semi_res->coh2F_det[i][j] != NULL ) {
              item->coh2F_det[i][j] = semi_res->coh2F_det[i][j][freq_idx];
            } else {
              // There is not per-detector F-statistic for this segment, usually because this segment contains
              // no data from this detector. In this case we output a clearly invalid F-statistic value.
              item->coh2F_det[i][j] = NAN;
            }
          }
        }
      }
    }

  }

  return XLAL_SUCCESS;

}

///
/// Write results toplist to a FITS file
///
int XLALWeaveResultsToplistWrite(
  FITSFile *file,
  const WeaveResultsToplist *toplist
  )
{

  // Check input
  XLAL_CHECK( file != NULL, XLAL_EFAULT );
  XLAL_CHECK( toplist != NULL, XLAL_EFAULT );

  // Write toplist
  {

    // Format name and description of statistic
    char name[256];
    snprintf( name, sizeof( name ), "%s_toplist", toplist->stat_name );
    char desc[256];
    snprintf( desc, sizeof( desc ), "toplist ranked by %s", toplist->stat_desc );

    // Open FITS table for writing and initialise
    XLAL_CHECK( XLALFITSTableOpenWrite( file, name, desc ) == XLAL_SUCCESS, XLAL_EFUNC );
    XLAL_CHECK( toplist_fits_table_init( file, toplist ) == XLAL_SUCCESS, XLAL_EFUNC );

    // Write all heap items to FITS table
    XLAL_CHECK( XLALHeapVisit( toplist->heap, toplist_fits_table_write_visitor, file ) == XLAL_SUCCESS, XLAL_EFUNC );

    // Write maximum size of heap to FITS header
    XLAL_CHECK( XLALFITSHeaderWriteINT4( file, "toplimit", XLALHeapMaxSize( toplist->heap ), "maximum size of toplist" ) == XLAL_SUCCESS, XLAL_EFUNC );

  }

  return XLAL_SUCCESS;

}

///
/// Read results from a FITS file and append to existing results toplist
///
int XLALWeaveResultsToplistReadAppend(
  FITSFile *file,
  WeaveResultsToplist *toplist
  )
{

  // Check input
  XLAL_CHECK( file != NULL, XLAL_EFAULT );
  XLAL_CHECK( toplist != NULL, XLAL_EFAULT );

  // Read and append to toplist
  {

    // Format name of statistic
    char name[256];
    snprintf( name, sizeof( name ), "%s_toplist", toplist->stat_name );

    // Open FITS table for reading and initialise
    UINT8 nrows = 0;
    XLAL_CHECK( XLALFITSTableOpenRead( file, name, &nrows ) == XLAL_SUCCESS, XLAL_EFUNC );
    XLAL_CHECK( toplist_fits_table_init( file, toplist ) == XLAL_SUCCESS, XLAL_EFUNC );

    // Read maximum size of heap from FITS header
    INT4 toplist_limit = 0;
    XLAL_CHECK( XLALFITSHeaderReadINT4( file, "toplimit", &toplist_limit ) == XLAL_SUCCESS, XLAL_EFUNC );

    // Maximize size of heap
    if ( toplist_limit > XLALHeapSize( toplist->heap ) ) {
      XLAL_CHECK( XLALHeapResize( toplist->heap, toplist_limit ) == XLAL_SUCCESS, XLAL_EFUNC );
    }

    // Read all items from FITS table
    while ( nrows > 0 ) {

      // Create a new toplist item if needed
      if ( toplist->saved_item == NULL ) {
        toplist->saved_item = toplist_item_create( toplist );
        XLAL_CHECK( toplist->saved_item != NULL, XLAL_ENOMEM );
      }

      // Read item from FITS table
      XLAL_CHECK( XLALFITSTableReadRow( file, toplist->saved_item, &nrows ) == XLAL_SUCCESS, XLAL_EFUNC );

      // Add item to heap
      XLAL_CHECK( XLALHeapAdd( toplist->heap, ( void ** ) &toplist->saved_item ) == XLAL_SUCCESS, XLAL_EFUNC );

    }

  }

  return XLAL_SUCCESS;

}

///
/// Compare two results toplists and return whether they are equal
///
int XLALWeaveResultsToplistCompare(
  BOOLEAN *equal,
  const WeaveSetupData *setup,
  const REAL8 param_tol_mism,
  const VectorComparison *result_tol,
  const WeaveResultsToplist *toplist_1,
  const WeaveResultsToplist *toplist_2
  )
{

  // Check input
  XLAL_CHECK( equal != NULL, XLAL_EFAULT );
  XLAL_CHECK( setup != NULL, XLAL_EFAULT );
  XLAL_CHECK( param_tol_mism > 0, XLAL_EINVAL );
  XLAL_CHECK( result_tol != NULL, XLAL_EFAULT );
  XLAL_CHECK( toplist_1 != NULL, XLAL_EFAULT );
  XLAL_CHECK( toplist_2 != NULL, XLAL_EFAULT );
  XLAL_CHECK( strcmp( toplist_1->stat_name, toplist_2->stat_name ) == 0, XLAL_EINVAL );
  XLAL_CHECK( strcmp( toplist_1->stat_desc, toplist_2->stat_desc ) == 0, XLAL_EINVAL );

  const WeaveResultsToplist *toplist = toplist_1;

  // Results toplists are assumed equal until we find otherwise
  *equal = 1;

  // Compare toplists
  XLALPrintInfo( "%s: comparing toplists ranked by %s ...\n", __func__, toplist->stat_desc );
  {

    // Compare lengths of heaps
    const size_t n = XLALHeapSize( toplist_1->heap );
    {
      const size_t n_2 = XLALHeapSize( toplist_2->heap );
      if ( n != n_2 ) {
        *equal = 0;
        XLALPrintInfo( "%s: unequal size %s toplists: %zu != %zu\n", __func__, toplist->stat_desc, n, n_2 );
        return XLAL_SUCCESS;
      }
    }

    // Get lists of toplist items
    const WeaveResultsToplistItem **items_1 = ( const WeaveResultsToplistItem ** ) XLALHeapElements( toplist_1->heap );
    XLAL_CHECK( items_1 != NULL, XLAL_EFUNC );
    const WeaveResultsToplistItem **items_2 = ( const WeaveResultsToplistItem ** ) XLALHeapElements( toplist_2->heap );
    XLAL_CHECK( items_2 != NULL, XLAL_EFUNC );

    // Sort toplist items by physical coordinates of semicoherent template
    // - Template coordinates are less likely to suffer from numerical differences
    //   than result values, and therefore provide more stable sort values to ensure
    //   that equivalent items in both templates match up with each other.
    // - Ideally one would compare toplist items with possess the minimum mismatch
    //   in template parameters with respect to each other, but that would require
    //   of order 'n^2' mismatch calculations, which may be too expensive
    qsort( items_1, n, sizeof( *items_1 ), toplist_item_sort_by_semi_phys );
    qsort( items_2, n, sizeof( *items_2 ), toplist_item_sort_by_semi_phys );

    // Allocate vectors for storing results for comparison with compare_vectors()
    REAL4Vector *res_1 = XLALCreateREAL4Vector( n );
    XLAL_CHECK( res_1 != NULL, XLAL_EFUNC );
    REAL4Vector *res_2 = XLALCreateREAL4Vector( n );
    XLAL_CHECK( res_2 != NULL, XLAL_EFUNC );

    do {   // So we can use 'break' to skip comparisons on failure

      // Compare semicoherent and coherent template parameters
      for ( size_t i = 0; i < n; ++i ) {
        char loc_str[256];

        // Compare semicoherent template parameters
        {
          snprintf( loc_str, sizeof(loc_str), "toplist item %zu", i );
          PulsarDopplerParams XLAL_INIT_DECL( semi_phys_1 );
          PulsarDopplerParams XLAL_INIT_DECL( semi_phys_2 );
          semi_phys_1.Alpha = items_1[i]->semi_alpha;
          semi_phys_2.Alpha = items_2[i]->semi_alpha;
          semi_phys_1.Delta = items_1[i]->semi_delta;
          semi_phys_2.Delta = items_2[i]->semi_delta;
          for ( size_t k = 0; k <= toplist->nspins; ++k ) {
            semi_phys_1.fkdot[k] = items_1[i]->semi_fkdot[k];
            semi_phys_2.fkdot[k] = items_2[i]->semi_fkdot[k];
          };
          XLAL_CHECK( compare_templates( equal, loc_str, "semicoherent", param_tol_mism, setup->phys_to_latt, setup->metrics->semi_rssky_metric, setup->metrics->semi_rssky_transf, &semi_phys_1, &semi_phys_2 ) == XLAL_SUCCESS, XLAL_EFUNC );
        }

        // Compare coherent template parameters
        for ( size_t j = 0; j < toplist->per_nsegments; ++j ) {
          snprintf( loc_str, sizeof(loc_str), "toplist item %zu, segment %zu", i, j );
          PulsarDopplerParams XLAL_INIT_DECL( coh_phys_1 );
          PulsarDopplerParams XLAL_INIT_DECL( coh_phys_2 );
          coh_phys_1.Alpha = items_1[i]->coh_alpha[j];
          coh_phys_2.Alpha = items_2[i]->coh_alpha[j];
          coh_phys_1.Delta = items_1[i]->coh_delta[j];
          coh_phys_2.Delta = items_2[i]->coh_delta[j];
          for ( size_t k = 0; k <= toplist->nspins; ++k ) {
            coh_phys_1.fkdot[k] = items_1[i]->coh_fkdot[k][j];
            coh_phys_2.fkdot[k] = items_2[i]->coh_fkdot[k][j];
          };
          XLAL_CHECK( compare_templates( equal, loc_str, "coherent", param_tol_mism, setup->phys_to_latt, setup->metrics->coh_rssky_metric[j], setup->metrics->coh_rssky_transf[j], &coh_phys_1, &coh_phys_2 ) == XLAL_SUCCESS, XLAL_EFUNC );
        }

      }
      if ( !*equal ) {
        break;
      }

      // Compare mean multi-detector F-statistics
      XLALPrintInfo( "%s: comparing mean multi-detector F-statistics ...\n", __func__ );
      for ( size_t i = 0; i < n; ++i ) {
        res_1->data[i] = items_1[i]->mean2F;
        res_2->data[i] = items_2[i]->mean2F;
      }
      XLAL_CHECK( compare_vectors( equal, result_tol, res_1, res_2 ) == XLAL_SUCCESS, XLAL_EFUNC );
      if ( !*equal ) {
        break;
      }

      // Compare mean per-detector F-statistic
      if ( toplist->per_detectors != NULL ) {
        for ( size_t k = 0; k < toplist->per_detectors->length; ++k ) {
          XLALPrintInfo( "%s: comparing mean per-detector F-statistics for detector '%s'...\n", __func__, toplist->per_detectors->data[k] );
          for ( size_t i = 0; i < n; ++i ) {
            res_1->data[i] = items_1[i]->mean2F_det[k];
            res_2->data[i] = items_2[i]->mean2F_det[k];
          }
          XLAL_CHECK( compare_vectors( equal, result_tol, res_1, res_2 ) == XLAL_SUCCESS, XLAL_EFUNC );
        }
        if ( !*equal ) {
          break;
        }
      }

      // Compare per-segment coherent multi-detector F-statistics
      for ( size_t j = 0; j < toplist->per_nsegments; ++j ) {
        XLALPrintInfo( "%s: comparing coherent multi-detector F-statistics for segment %zu...\n", __func__, j );
        for ( size_t i = 0; i < n; ++i ) {
          res_1->data[i] = items_1[i]->coh2F[j];
          res_2->data[i] = items_2[i]->coh2F[j];
        }
        XLAL_CHECK( compare_vectors( equal, result_tol, res_1, res_2 ) == XLAL_SUCCESS, XLAL_EFUNC );
      }
      if ( !*equal ) {
        break;
      }

      // Compare per-segment per-detector F-statistics
      if ( toplist->per_detectors != NULL ) {
        for ( size_t j = 0; j < toplist->per_nsegments; ++j ) {
          for ( size_t k = 0; k < toplist->per_detectors->length; ++k ) {
            if ( isfinite( items_1[0]->coh2F_det[k][j] ) || isfinite( items_2[0]->coh2F_det[k][j] ) ) {
              XLALPrintInfo( "%s: comparing per-segment per-detector F-statistics for segment %zu, detector '%s'...\n", __func__, j, toplist->per_detectors->data[k] );
              for ( size_t i = 0; i < n; ++i ) {
                res_1->data[i] = items_1[i]->coh2F_det[k][j];
                res_2->data[i] = items_2[i]->coh2F_det[k][j];
              }
              XLAL_CHECK( compare_vectors( equal, result_tol, res_1, res_2 ) == XLAL_SUCCESS, XLAL_EFUNC );
            } else {
              XLALPrintInfo( "%s: no per-segment per-detector F-statistics for segment %zu, detector '%s'; skipping comparison\n", __func__, j, toplist->per_detectors->data[k] );
            }
          }
        }
      }
      if ( !*equal ) {
        break;
      }

    } while (0);

    // Cleanup
    XLALFree( items_1 );
    XLALFree( items_2 );
    XLALDestroyREAL4Vector( res_1 );
    XLALDestroyREAL4Vector( res_2 );

    if ( !*equal ) {
      return XLAL_SUCCESS;
    }

  }

  return XLAL_SUCCESS;

}

// Local Variables:
// c-file-style: "linux"
// c-basic-offset: 2
// End:
