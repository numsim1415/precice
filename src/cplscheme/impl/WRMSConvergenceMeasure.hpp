#pragma once

#include "ConvergenceMeasure.hpp"
#include "utils/Helpers.hpp"
#include "utils/Dimensions.hpp"
#include "tarch/logging/Log.h"
#include "utils/MasterSlave.hpp"

namespace precice {
   namespace cplscheme {
      namespace tests {
         //class AbsoluteConvergenceMeasureTest;
      }
   }
}

// ------------------------------------------------------------ CLASS DEFINTION

namespace precice {
namespace cplscheme {
namespace impl {

/**
 * @brief Measures the convergence from an old data set to a new one.
 *
 * The convergence is evaluated by looking if the weighted residual mean square norm is smaller than 1.0 .
 * As weights the coupling data from last timestep is used.
 *
 * For a description of how to perform the measurement, see class ConvergenceMeasure.
 */
class WRMSConvergenceMeasure : public ConvergenceMeasure
{
public:

  WRMSConvergenceMeasure ( double relTol, double absTol );

   virtual ~WRMSConvergenceMeasure() {};

   virtual void newMeasurementSeries (const utils::DynVector& oldValues)
   {
     preciceTrace("newMeasurementSeries()");
      _isConvergence = false;
      if(_weights.size()==0){ //first call
        _weights.append(oldValues.size(), 0.0);
      }
      for(int i=0; i<oldValues.size(); i++){
        _weights[i] = 1.0 / (std::abs(oldValues[i]) * _relTol + _absTol);
      }
   }

   virtual void measure (
      const utils::DynVector& oldValues,
      const utils::DynVector& newValues )
   {
     preciceTrace("measure()");
      _normDiff = utils::MasterSlave::wrmsNorm(newValues - oldValues, _weights);
      _isConvergence = _normDiff <= 1.0;
   }

   virtual bool isConvergence () const
   {
      return _isConvergence;
   }

   /**
    * @brief Adds current convergence information to output stream.
    */
   virtual std::string printState()
   {
     std::ostringstream os;
     os << "wrms convergence measure: ";
     os << "wrms diff = " << _normDiff;
     os << ", limit = 1.0";
     os << ", conv = ";
     if (_isConvergence) os << "true";
     else os << "false";
     return os.str();
   }
   
   virtual double getNormResidual()
   {
    return _normDiff; 
   }

private:

   static tarch::logging::Log _log;

   double _normDiff;

   double _relTol;

   double _absTol;

   bool _isConvergence;

   utils::DynVector _weights;

};

}}} // namespace precice, cplscheme, impl

