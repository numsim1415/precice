// Copyright (C) 2011 Technische Universitaet Muenchen
// This file is part of the preCICE project. For conditions of distribution and
// use, please see the license notice at http://www5.in.tum.de/wiki/index.php/PreCICE_License
#include "WRMSConvergenceMeasure.hpp"

namespace precice {
namespace cplscheme {
namespace impl {

tarch::logging::Log WRMSConvergenceMeasure::
   _log ( "precice::cplscheme::WRMSConvergenceMeasure" );

WRMSConvergenceMeasure:: WRMSConvergenceMeasure
(
    double relTol, double absTol)
:
   ConvergenceMeasure (),
   _normDiff(0.0),
   _relTol ( relTol),
   _absTol (absTol),
   _isConvergence ( false )
{
   preciceCheck ( ! tarch::la::greaterEquals(0.0, _absTol),
                  "WRMSConvergenceMeasure()", "Absolute tolerance "
                  << "has to be greater than zero!" );
   preciceCheck ( ! tarch::la::greaterEquals(0.0, _relTol),
                  "WRMSConvergenceMeasure()", "Relative tolerance "
                  << "has to be greater than zero!" );
}

}}} // namespace precice, cplscheme
