#include "AitkenPostProcessing.hpp"
#include "../CouplingData.hpp"
#include "mesh/Data.hpp"
#include "mesh/Vertex.hpp"
#include "mesh/Mesh.hpp"
#include "utils/Globals.hpp"
#include "utils/MasterSlave.hpp"
#include <Eigen/Dense>
#include "utils/EigenHelperFunctions.hpp"
#include <limits>
#include "math/math.hpp"

namespace precice {
namespace cplscheme {
namespace impl {

logging::Logger AitkenPostProcessing::
  _log ( "precice::cplscheme::AitkenPostProcessing" );

AitkenPostProcessing:: AitkenPostProcessing
(
  double initialRelaxation,
  std::vector<int>    dataIDs )
:
  PostProcessing (),
  _initialRelaxation ( initialRelaxation ),
  _dataIDs ( dataIDs ),
  _aitkenFactor ( initialRelaxation ),
  _iterationCounter ( 0 ),
  _residuals (),
  _designSpecification ()
{
  preciceCheck ( (_initialRelaxation > 0.0) && (_initialRelaxation <= 1.0),
                 "AitkenPostProcessing()",
                 "Initial relaxation factor for aitken post processing has to "
                 << "be larger than zero and smaller or equal than one!" );
}

void AitkenPostProcessing:: initialize
(
  DataMap& cplData )
{
  preciceCheck(utils::contained(*_dataIDs.begin(), cplData), "initialize()",
               "Data with ID " << *_dataIDs.begin()
               << " is not contained in data given at initialization!" );
  size_t entries=0;
  if(_dataIDs.size()==1){
    entries = cplData[_dataIDs.at(0)]->values->size();
  }
  else{
    assertion(_dataIDs.size()==2);
    entries = cplData[_dataIDs.at(0)]->values->size() +
        cplData[_dataIDs.at(1)]->values->size();
  }
  double initializer = std::numeric_limits<double>::max();
  Eigen::VectorXd toAppend = Eigen::VectorXd::Constant(entries, initializer);
  utils::append(_residuals, toAppend);
  _designSpecification = Eigen::VectorXd::Zero(entries);

  // Append column for old values if not done by coupling scheme yet
  for (DataMap::value_type& pair : cplData) {
    int cols = pair.second->oldValues.cols();
    if (cols < 1){
      assertion(pair.second->values->size() > 0, pair.first);
      utils::append(pair.second->oldValues,
          (Eigen::VectorXd) Eigen::VectorXd::Zero(pair.second->values->size()));
    }
  }
}

void AitkenPostProcessing:: performPostProcessing
(
  DataMap& cplData )
{
  preciceTrace("performPostProcessing()");

  // Compute aitken relaxation factor
  assertion(utils::contained(*_dataIDs.begin(), cplData));

  Eigen::VectorXd values;
  Eigen::VectorXd oldValues;
  for (int id : _dataIDs) {
    utils::append(values, *(cplData[id]->values));
    utils::append(oldValues, (Eigen::VectorXd) cplData[id]->oldValues.col(0));
  }

  // Compute current residuals
  Eigen::VectorXd residuals = values;
  residuals -= oldValues;

  // Compute residual deltas and temporarily store it in _residuals
  Eigen::VectorXd residualDeltas = _residuals;
  residualDeltas *= -1.0;
  residualDeltas += residuals;

  // Select/compute aitken factor depending on current iteration count
  if (_iterationCounter == 0){
    _aitkenFactor = math::sign(_aitkenFactor) * min(
                    utils::Vector2D(_initialRelaxation, std::abs(_aitkenFactor)));
  }
  else {
    // compute fraction of aitken factor with residuals and residual deltas
    double nominator = utils::MasterSlave::dot(_residuals, residualDeltas);
    double denominator = utils::MasterSlave::dot(residualDeltas, residualDeltas);
    _aitkenFactor = -_aitkenFactor * (nominator / denominator);
  }

  DEBUG("AitkenFactor: " << _aitkenFactor);

  // Perform relaxation with aitken factor
  double omega = _aitkenFactor;
  double oneMinusOmega = 1.0 - omega;
  for (DataMap::value_type& pair : cplData) {
    auto& values = *pair.second->values;
    const auto& oldValues = pair.second->oldValues.col(0);
    values *= omega;
    for ( int i=0; i < values.size(); i++ ) {
      values(i) += oldValues(i) * oneMinusOmega;
    }
  }

  // Store residuals for next iteration
  _residuals = residuals;

  _iterationCounter++;
}

void AitkenPostProcessing:: iterationsConverged
(
  DataMap& cplData )
{
  _iterationCounter = 0;
  _residuals = Eigen::VectorXd::Constant(_residuals.size(), std::numeric_limits<double>::max());
}

/** ---------------------------------------------------------------------------------------------
 *         getDesignSpecification()
 *
 * @brief: Returns the design specification corresponding to the given coupling data.
 *         This information is needed for convergence measurements in the coupling scheme.
 *  ---------------------------------------------------------------------------------------------
 */        // TODO: change to call by ref when Eigen is used.
std::map<int, Eigen::VectorXd> AitkenPostProcessing::getDesignSpecification
(
  DataMap& cplData)
{
  std::map<int, Eigen::VectorXd> designSpecifications;
  int off = 0;
  for (int id : _dataIDs) {
      int size = cplData[id]->values->size();
      Eigen::VectorXd q = Eigen::VectorXd::Zero(size);
      for (int i = 0; i < size; i++) {
        q(i) = _designSpecification(i+off);
      }
      off += size;
      std::map<int, Eigen::VectorXd>::value_type pair = std::make_pair(id, q);
      designSpecifications.insert(pair);
    }
  return designSpecifications;
}

void AitkenPostProcessing::setDesignSpecification(
     Eigen::VectorXd& q)
 {
   _designSpecification = q;
   preciceError(__func__, "design specification for Aitken relaxation is not supported yet.");
 }



}}} // namespace precice, cplscheme, impl
