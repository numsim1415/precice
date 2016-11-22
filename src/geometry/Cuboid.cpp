#include "geometry/Cuboid.hpp"
#include "mesh/Mesh.hpp"
#include "mesh/Vertex.hpp"
#include "mesh/Edge.hpp"
#include "mesh/Triangle.hpp"
#include "mesh/PropertyContainer.hpp"

namespace precice {
namespace geometry {

logging::Logger Cuboid:: _log ( "precice::geometry::Cuboid" );

Cuboid:: Cuboid
(
  const Eigen::VectorXd& offset,
  double                 discretizationWidth,
  const Eigen::VectorXd& sidelengths )
:
  Geometry ( offset ),
  _discretizationWidth ( discretizationWidth ),
  _sidelengths ( sidelengths )
{}

void Cuboid:: specializedCreate
(
  mesh::Mesh& seed )
{
  TRACE();
  std::string nameSubID ( seed.getName() + "-side-" );
  int dimensions = seed.getDimensions();
  assertion ( (dimensions == 2) || (dimensions == 3), dimensions );
  assertion ( dimensions == _sidelengths.size(), dimensions, _sidelengths.size() );
  if ( dimensions == 2 ){
    // Create corners
    std::array<Eigen::Vector2d, 4> cornerCoords;
    std::array<mesh::Vertex*,4> cornerVertices;
    Eigen::Vector2d halfSidelengths ( _sidelengths );
    halfSidelengths *= 0.5;
    for ( int i=0; i < 4; i++ ) {
      Eigen::Vector2d delinearized(utils::delinearize(i, 2));
      cornerCoords[i] = delinearized.cwiseProduct(halfSidelengths) + halfSidelengths;
      cornerVertices[i] = &seed.createVertex ( cornerCoords[i] );
    }

    // Determine mesh width h
    Eigen::Vector2i vertexCount;
    Eigen::Vector2d h;
    for ( int i=0; i < 2; i++) {
      vertexCount[i] = (int)std::floor ( _sidelengths(i) / _discretizationWidth );
      if ( vertexCount(i) > 0.0 ) {
        h(i) = _sidelengths(i) / vertexCount(i);
      }
      else {
        h(i) = _sidelengths(i);
      }
    }
    mesh::PropertyContainer* parent = nullptr;

    // Create sides of 2d cuboid and assign sub-ids

    // Side 2 (lower side, left to right)
    mesh::Vertex* oldVertex = cornerVertices[0]; // vertex0;
    bool hasIDSide2 = false;
    if ( seed.getNameIDPairs().count(nameSubID + "2") ) {
      hasIDSide2 = true;
      parent = & seed.getPropertyContainer ( nameSubID + "2" );
      oldVertex->addParent ( *parent );
      cornerVertices[1]->addParent ( *parent );
    }
    for ( int i=1; i < vertexCount[0]; i++ ) {
      mesh::Vertex* newVertex = & seed.createVertex (
        Eigen::Vector2d(cornerCoords[0](0) + ((double)i / (double)vertexCount[0]) * _sidelengths[0],
                        cornerCoords[0](1)) );
      mesh::Edge& edge = seed.createEdge ( *oldVertex, *newVertex );
      if ( hasIDSide2 ) {
        edge.addParent ( *parent );
        newVertex->addParent ( *parent );
      }
      oldVertex = newVertex;
    }
    mesh::Edge& edge2 = seed.createEdge ( *oldVertex, *cornerVertices[1] );
    if ( hasIDSide2 ) {
      edge2.addParent ( *parent );
    }

    // Side 3 (upper side, right to left)
    oldVertex = cornerVertices[3];
    bool hasIDSide3 = false;
    if ( seed.getNameIDPairs().count(nameSubID + "3") ) {
      hasIDSide3 = true;
      parent = & seed.getPropertyContainer ( nameSubID + "3" );
      oldVertex->addParent ( *parent );
      cornerVertices[2]->addParent ( *parent );
    }
    for ( int i=1; i < vertexCount[0]; i++ ) {
      mesh::Vertex* newVertex = & seed.createVertex (
        Eigen::Vector2d(cornerCoords[3](0) - ((double)i / (double)vertexCount[0]) * _sidelengths[0],
                      cornerCoords[3](1)) );
      mesh::Edge& edge = seed.createEdge (*oldVertex, *newVertex);
      if ( hasIDSide3 ) {
        edge.addParent ( *parent );
        newVertex->addParent ( *parent );
      }
      oldVertex = newVertex;
    }
    mesh::Edge& edge3 = seed.createEdge ( *oldVertex, *cornerVertices[2] );
    if ( hasIDSide3 ) {
      edge3.addParent ( *parent );
    }

    // Side 0 (left side, top to bottom)
    oldVertex = cornerVertices[2];
    bool hasIDSide0 = false;
    if ( seed.getNameIDPairs().count(nameSubID + "0") ) {
      hasIDSide0 = true;
      parent = & seed.getPropertyContainer ( nameSubID + "0" );
      oldVertex->addParent ( *parent );
      cornerVertices[0]->addParent ( *parent );
    }
    for ( int i=1; i < vertexCount[1]; i++ ) {
      mesh::Vertex* newVertex = & seed.createVertex (
        Eigen::Vector2d(cornerCoords[2](0),
                        cornerCoords[2](1) - ((double)i / (double)vertexCount[1]) * _sidelengths[1]) );
      mesh::Edge& edge = seed.createEdge ( *oldVertex, *newVertex );
      if ( hasIDSide0 ) {
        edge.addParent ( *parent );
        newVertex->addParent ( *parent );
      }
      oldVertex = newVertex;
    }
    mesh::Edge& edge0 = seed.createEdge ( *oldVertex, *cornerVertices[0] );
    if ( hasIDSide0 ) {
      edge0.addParent ( *parent );
    }

    // Side 1 (right side, bottom to top)
    oldVertex = cornerVertices[1];
    bool hasIDSide1 = false;
    if ( seed.getNameIDPairs().count(nameSubID + "1") ) {
      hasIDSide1 = true;
      parent = & seed.getPropertyContainer ( nameSubID + "1" );
      oldVertex->addParent ( *parent );
      cornerVertices[3]->addParent ( *parent );
    }
    for ( int i=1; i < vertexCount[1]; i++ ) {
      mesh::Vertex * newVertex = & seed.createVertex (
        Eigen::Vector2d(cornerCoords[1](0),
                        cornerCoords[1](1) + ((double)i / (double)vertexCount[1]) * _sidelengths[1]) );
      mesh::Edge& edge = seed.createEdge ( *oldVertex, *newVertex );
      if ( hasIDSide1 ) {
        edge.addParent ( *parent );
        newVertex->addParent ( *parent );
      }
      oldVertex = newVertex;
    }
    mesh::Edge& edge1 = seed.createEdge ( *oldVertex, *cornerVertices[3] );
    if ( hasIDSide1 ) {
      edge1.addParent ( *parent );
    }
  }

  else { // Create 3D Hexahedron
    assertion ( dimensions == 3, dimensions );
    // Create corners
    std::array<Eigen::Vector3d,8> cornerCoords;
    std::array<mesh::Vertex*,8> cornerVertices;
    Eigen::Vector3d halfSidelengths ( _sidelengths );
    halfSidelengths *= 0.5;
    for ( int i=0; i < 8; i++ ){
      Eigen::Vector3d delinearized(utils::delinearize(i, 3));
      cornerCoords[i] = delinearized.cwiseProduct(halfSidelengths) + halfSidelengths;
      cornerVertices[i] = &seed.createVertex ( cornerCoords[i] );
    }

    mesh::PropertyContainer* parent = nullptr;

    // Create cuboid edges
    std::array<mesh::Edge*,12> edges;
    for ( int edgeIndex=0; edgeIndex < 12; edgeIndex++ ){
      int index0 = utils::IndexMaps<3>::CUBOID_EDGE_VERTICES[edgeIndex][0];
      int index1 = utils::IndexMaps<3>::CUBOID_EDGE_VERTICES[edgeIndex][1];
      edges[edgeIndex] = & seed.createEdge ( *cornerVertices[index0],
                                             *cornerVertices[index1] );
    }

    // Create face crossing edges and triangles
    for ( int faceIndex=0; faceIndex < 6; faceIndex++ ) {
      int vertexIndices[4];
      int edgeIndices[4];
      for ( int i=0; i < 4; i++ ) {
        vertexIndices[i] = utils::IndexMaps<3>::CUBOID_FACE_VERTICES[faceIndex][i];
        edgeIndices[i] = utils::IndexMaps<3>::CUBOID_FACE_EDGES[faceIndex][i];
      }
      // For every dimension, there is a coordinate wise low and high placed face
      // of a cuboid, which have opposite vertex orderings
      bool lowside = faceIndex % 2 == 0 ? true : false;
      // The faces of dimension 1 are ordered reversed to the other 2 dimensions
      int dim = faceIndex / 2;
      lowside = (dim != 1) ? lowside : ! lowside;
      // Create edge crossing cuboid face
      mesh::Edge & crossingEdge = seed.createEdge (
        *cornerVertices[vertexIndices[1]],
        *cornerVertices[vertexIndices[2]] );
      mesh::Triangle* t0 = nullptr;
      mesh::Triangle* t1 = nullptr;
      if ( lowside ){
        t0 = & seed.createTriangle (
          *edges[edgeIndices[0]], *edges[edgeIndices[2]], crossingEdge );
        t1 = & seed.createTriangle (
          *edges[edgeIndices[1]], *edges[edgeIndices[3]], crossingEdge );
      }
      else {
        t0 = & seed.createTriangle (
          *edges[edgeIndices[2]], *edges[edgeIndices[0]], crossingEdge );
        t1 = & seed.createTriangle (
          *edges[edgeIndices[3]], *edges[edgeIndices[1]], crossingEdge );
      }
      std::ostringstream stream;
      stream << faceIndex;
      if ( seed.getNameIDPairs().count(nameSubID + stream.str()) ) {
        parent = & seed.getPropertyContainer ( nameSubID + stream.str() );
        for ( int i=0; i < 4; i++ ) {
          cornerVertices[vertexIndices[i]]->addParent ( *parent );
          edges[edgeIndices[i]]->addParent ( *parent );
        }
        t0->addParent ( *parent );
        t1->addParent ( *parent );
        crossingEdge.addParent ( *parent );
      }
    }
  }
}

}} // namespace precice, geometry
