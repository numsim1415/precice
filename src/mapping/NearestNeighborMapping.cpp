#include "NearestNeighborMapping.hpp"
#include "query/FindClosestVertex.hpp"
#include "utils/Helpers.hpp"
#include <Eigen/Dense>

namespace precice {
namespace mapping {

logging::Logger NearestNeighborMapping::_log("precice::mapping::NearestNeighborMapping");

NearestNeighborMapping:: NearestNeighborMapping
(
  Constraint constraint,
  int        dimensions)
:
  Mapping(constraint, dimensions),
  _hasComputedMapping(false),
  _vertexIndices()
{
  setInputRequirement(VERTEX);
  setOutputRequirement(VERTEX);
}

void NearestNeighborMapping:: computeMapping()
{
  TRACE(input()->vertices().size());
  assertion(input().get() != nullptr);
  assertion(output().get() != nullptr);
  if (getConstraint() == CONSISTENT){
    DEBUG("Compute consistent mapping");
    size_t verticesSize = output()->vertices().size();
    _vertexIndices.resize(verticesSize);
    const mesh::Mesh::VertexContainer& outputVertices = output()->vertices();
    for ( size_t i=0; i < verticesSize; i++ ){
      const Eigen::VectorXd& coords = outputVertices[i].getCoords();
      query::FindClosestVertex find(coords); // Search for the output vertex ...
      find(*input()); // ... inside the input mesh
      assertion(find.hasFound());
      _vertexIndices[i] = find.getClosestVertex().getID();
    }
  }
  else {
    assertion(getConstraint() == CONSERVATIVE, getConstraint());
    DEBUG("Compute conservative mapping");
    size_t verticesSize = input()->vertices().size();
    _vertexIndices.resize(verticesSize);
    const mesh::Mesh::VertexContainer& inputVertices = input()->vertices();
    for ( size_t i=0; i < verticesSize; i++ ){
      const Eigen::VectorXd& coords = inputVertices[i].getCoords();
      query::FindClosestVertex find(coords); // Search for the input vertex ...
      find(*output()); // ... inside the output mesh
      assertion(find.hasFound());
      _vertexIndices[i] = find.getClosestVertex().getID();
    }
  }
  _hasComputedMapping = true;
}

bool NearestNeighborMapping:: hasComputedMapping() const
{
  TRACE(_hasComputedMapping);
  return _hasComputedMapping;
}

void NearestNeighborMapping:: clear()
{
  TRACE();
  _vertexIndices.clear();
  _hasComputedMapping = false;
}

void NearestNeighborMapping:: map
(
  int inputDataID,
  int outputDataID )
{
  TRACE(inputDataID, outputDataID);
  const Eigen::VectorXd& inputValues = input()->data(inputDataID)->values();
  Eigen::VectorXd& outputValues = output()->data(outputDataID)->values();
  //assign(outputValues) = 0.0;
  int valueDimensions = input()->data(inputDataID)->getDimensions();
  assertion ( valueDimensions == output()->data(outputDataID)->getDimensions(),
              valueDimensions, output()->data(outputDataID)->getDimensions() );
  assertion ( inputValues.size() / valueDimensions == (int)input()->vertices().size(),
               inputValues.size(), valueDimensions, input()->vertices().size() );
  assertion ( outputValues.size() / valueDimensions == (int)output()->vertices().size(),
               outputValues.size(), valueDimensions, output()->vertices().size() );
  if (getConstraint() == CONSISTENT){
    DEBUG("Map consistent");
    size_t outSize = output()->vertices().size();
    for ( size_t i=0; i < outSize; i++ ){
      int inputIndex = _vertexIndices[i] * valueDimensions;
      for ( int dim=0; dim < valueDimensions; dim++ ){
        outputValues((i*valueDimensions)+dim) = inputValues(inputIndex+dim);
      }
    }
  }
  else {
    assertion(getConstraint() == CONSERVATIVE, getConstraint());
    DEBUG("Map conservative");
    size_t inSize = input()->vertices().size();
    for ( size_t i=0; i < inSize; i++ ){
      int outputIndex = _vertexIndices[i] * valueDimensions;
      for ( int dim=0; dim < valueDimensions; dim++ ){
        outputValues(outputIndex+dim) += inputValues((i*valueDimensions)+dim);
      }
    }
  }
}

bool NearestNeighborMapping::doesVertexContribute(
  int vertexID) const
{
  return utils::contained(vertexID,_vertexIndices);
}

bool NearestNeighborMapping:: isProjectionMapping() const
{
  return true;
}

}} // namespace precice, mapping
