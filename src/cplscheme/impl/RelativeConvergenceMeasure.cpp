#include "RelativeConvergenceMeasure.hpp"
#include "utils/Globals.hpp"

namespace precice {
namespace cplscheme {
namespace impl {

logging::Logger RelativeConvergenceMeasure::
   _log ( "precice::cplscheme::RelativeConvergenceMeasure" );


RelativeConvergenceMeasure:: RelativeConvergenceMeasure
(
   double convergenceLimitPercent )
:
   ConvergenceMeasure (),
   _convergenceLimitPercent ( convergenceLimitPercent ),
   _normDiff(0.0),
   _norm(0.0),
   _isConvergence ( false )
{
   CHECK ( math::greater(_convergenceLimitPercent, 0.0) && math::greaterEquals(1.0, _convergenceLimitPercent),
           "Relative convergence limit has to be in ]0;1] !" );
}

//void RelativeConvergenceMeasure:: startMeasurement ()
//{
//   _sumSquaredDifferences = 0.0;
//   _sumSquaredNew = 0.0;
//   _isConvergence = false;
//}

//void RelativeConvergenceMeasure:: finishMeasurement ()
//{
//   double twoNormDifferences = std::sqrt ( _sumSquaredDifferences );
//   double twoNormNew = std::sqrt ( _sumSquaredNew );
//   _isConvergence = tarch::la::greaterEquals (
//         twoNormNew * _convergenceLimitPercent, twoNormDifferences );
//   preciceInfo ( "finsihMeasurement()", "Relative convergence measure: "
//                 << "two-norm differences = " << twoNormDifferences
//                 << ", convergence limit = "
//                 << twoNormNew * _convergenceLimitPercent
//                 << ", convergence = " << _isConvergence );
//}

}}} // namespace precice, cplscheme, impl
