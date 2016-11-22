#include "Triangle.hpp"
#include "mesh/Edge.hpp"
#include "mesh/Vertex.hpp"
#include "utils/ManageUniqueIDs.hpp"

namespace precice {
namespace mesh {

Triangle:: Triangle
(
  Edge& edgeOne,
  Edge& edgeTwo,
  Edge& edgeThree,
  int   id )
:
  PropertyContainer (),
  _edges( { &edgeOne, &edgeTwo, &edgeThree} ),
  _vertexMap (),
  _id ( id ),
  _normal ( edgeOne.getDimensions() ),
  _center ( edgeOne.getDimensions() ),
  _enclosingRadius ( 0.0 )
{
  assertion ( edgeOne.getDimensions() == edgeTwo.getDimensions(),
               edgeOne.getDimensions(), edgeTwo.getDimensions() );
  assertion ( edgeTwo.getDimensions() == edgeThree.getDimensions(),
               edgeTwo.getDimensions(), edgeThree.getDimensions() );
  assertion ( getDimensions() == 3, getDimensions() );

  // Determine vertex map
  Vertex& v0 = edge(0).vertex(0);
  Vertex& v1 = edge(0).vertex(1);

  if ( &edge(1).vertex(0) == &v0 ){
    _vertexMap[0] = 1;
    _vertexMap[1] = 0;
  }
  else if ( &edge(1).vertex(1) == &v0 ){
    _vertexMap[0] = 1;
    _vertexMap[1] = 1;
  }
  else if ( &edge(1).vertex(0) == &v1 ){
    _vertexMap[0] = 0;
    _vertexMap[1] = 0;
  }
  else {
    assertion ( &edge(1).vertex(1) == &v1 );
    _vertexMap[0] = 0;
    _vertexMap[1] = 1;
  }

  if ( _vertexMap[1] == 0 ){
    if ( &edge(2).vertex(0) == &edge(1).vertex(1) ){
      _vertexMap[2] = 0;
    }
    else {
      assertion ( &edge(2).vertex(1) == &edge(1).vertex(1) );
      _vertexMap[2] = 1;
    }
  }
  else if ( _vertexMap[1] == 1 ){
    if ( &edge(2).vertex(0) == &edge(1).vertex(0) ){
      _vertexMap[2] = 0;
    }
    else {
      assertion ( &edge(2).vertex(1) == &edge(1).vertex(0) );
      _vertexMap[2] = 1;
    }
  }
  assertion ( &edge(0).vertex(_vertexMap[0]) != &edge(1).vertex(_vertexMap[1]) );
  assertion ( &edge(0).vertex(_vertexMap[0]) != &edge(2).vertex(_vertexMap[2]) );
  assertion ( &edge(1).vertex(_vertexMap[1]) != &edge(2).vertex(_vertexMap[2]) );
  assertion ( (_vertexMap[0] == 0) || (_vertexMap[0] == 1), _vertexMap[0] );
  assertion ( (_vertexMap[1] == 0) || (_vertexMap[1] == 1), _vertexMap[0] );
  assertion ( (_vertexMap[2] == 0) || (_vertexMap[2] == 1), _vertexMap[0] );
}

int Triangle:: getDimensions() const
{
  return _edges[0]->getDimensions();
}

void Triangle:: setEnclosingRadius
(
  double radius )
{
  _enclosingRadius = radius;
}

const Eigen::VectorXd& Triangle:: getNormal() const
{
  return _normal;
}

const Eigen::VectorXd& Triangle:: getCenter() const
{
  return _center;
}

double Triangle:: getEnclosingRadius() const
{
  return _enclosingRadius;
}

}} // namespace precice, mesh
