#include "WatchPoint.hpp"
#include "query/FindClosestVertex.hpp"
#include "query/FindClosestEdge.hpp"
#include "query/FindClosestTriangle.hpp"
#include "mesh/Mesh.hpp"
#include "mesh/Vertex.hpp"
#include "mesh/Edge.hpp"
#include "mesh/Triangle.hpp"
#include "mesh/Data.hpp"
#include "utils/Globals.hpp"
#include "utils/MasterSlave.hpp"
#include "com/Communication.hpp"
#include <limits>

namespace precice {
namespace impl {

logging::Logger WatchPoint:: _log ( "precice::impl::WatchPoint" );

WatchPoint:: WatchPoint
(
  const Eigen::VectorXd&  pointCoords,
  const mesh::PtrMesh&    meshToWatch,
  const std::string&      exportFilename )
:
  _point ( pointCoords ),
  _mesh ( meshToWatch ),
  _txtWriter ( exportFilename ),
  _shortestDistance ( std::numeric_limits<double>::max() ),
  _weights (),
  _vertices (),
  _dataToExport (),
  _isClosest(true)
{
  assertion ( _mesh.use_count() > 0 );
  assertion ( _point.size() == _mesh->getDimensions(), _point.size(),
               _mesh->getDimensions() );
}

const mesh::PtrMesh& WatchPoint:: mesh() const
{
  return _mesh;
}

void WatchPoint:: initialize()
{
  preciceTrace ( "initialize()");
  // Find closest vertex
  if(_mesh->vertices().size()>0){
    query::FindClosestVertex findVertex ( _point );
    findVertex ( *_mesh );
    _vertices.push_back ( & findVertex.getClosestVertex() );
    _shortestDistance = findVertex.getEuclidianDistance ();
    _weights.push_back ( 1.0 );
  }

  if(utils::MasterSlave::_slaveMode){
    utils::MasterSlave::_communication->send(_shortestDistance, 0);
    utils::MasterSlave::_communication->receive(_isClosest, 0);
  }

  if(utils::MasterSlave::_masterMode){
    int closestRank = 0;
    double closestDistanceGlobal = _shortestDistance;
    double closestDistanceLocal = std::numeric_limits<double>::max();
    for(int rankSlave = 1; rankSlave < utils::MasterSlave::_size; rankSlave++){
      utils::MasterSlave::_communication->receive(closestDistanceLocal, rankSlave);
      if(closestDistanceLocal < closestDistanceGlobal){
        closestDistanceGlobal = closestDistanceLocal;
        closestRank = rankSlave;
      }
    }
    _isClosest = closestRank == 0;
    for(int rankSlave = 1; rankSlave < utils::MasterSlave::_size; rankSlave++){
      utils::MasterSlave::_communication->send(closestRank==rankSlave, rankSlave);
    }
  }

  DEBUG("Rank: " << utils::MasterSlave::_rank << ", isClosest: " << _isClosest);

  if(_isClosest){

    // Find closest edge
    query::FindClosestEdge findEdge ( _point );
    if ( findEdge(*_mesh) ) {
      if ( findEdge.getEuclidianDistance() < _shortestDistance ) {
        //         _closestEdge = & findEdge.getClosestEdge ();
        _vertices.clear ();
        _vertices.push_back ( & findEdge.getClosestEdge().vertex(0) );
        _vertices.push_back ( & findEdge.getClosestEdge().vertex(1) );
        _shortestDistance = findEdge.getEuclidianDistance ();
        _weights.clear ();
        _weights.push_back ( findEdge.getProjectionPointParameter(0) );
        _weights.push_back ( findEdge.getProjectionPointParameter(1) );
      }
    }
    if ( _mesh->getDimensions() == 3 ) {
      // Find closest triangle
      query::FindClosestTriangle findTriangle ( _point );
      if ( findTriangle(*_mesh) ) {
        if ( findTriangle.getEuclidianDistance() < _shortestDistance ) {
          _vertices.clear ();
          _vertices.push_back ( & findTriangle.getClosestTriangle().vertex(0) );
          _vertices.push_back ( & findTriangle.getClosestTriangle().vertex(1) );
          _vertices.push_back ( & findTriangle.getClosestTriangle().vertex(2) );
          _shortestDistance = findTriangle.getEuclidianDistance ();
          _weights.clear ();
          _weights.push_back ( findTriangle.getProjectionPointParameter(0) );
          _weights.push_back ( findTriangle.getProjectionPointParameter(1) );
          _weights.push_back ( findTriangle.getProjectionPointParameter(2) );
        }
      }
    }

    io::TXTTableWriter::DataType vectorType = _mesh->getDimensions() == 2
        ? io::TXTTableWriter::VECTOR2D
        : io::TXTTableWriter::VECTOR3D;
    _txtWriter.addData("Time", io::TXTTableWriter::DOUBLE);
    _txtWriter.addData("Coordinate", vectorType);
    for (size_t i=0; i < _mesh->data().size(); i++){
      _dataToExport.push_back(_mesh->data()[i]);
      if (_dataToExport[i]->getDimensions() > 1){
        _txtWriter.addData(_dataToExport[i]->getName(), vectorType);
      }
      else {
        _txtWriter.addData(_dataToExport[i]->getName(), io::TXTTableWriter::DOUBLE);
      }
    }

  } //isClosest
}

void WatchPoint:: exportPointData
(
  double time )
{
  if(_isClosest){
    assertion(_vertices.size() == _weights.size());
    _txtWriter.writeData("Time", time);
    // Export watch point coordinates
    Eigen::VectorXd coords = Eigen::VectorXd::Constant(_mesh->getDimensions(), 0.0);
    for (size_t i=0; i < _vertices.size(); i++){
      coords += _weights[i] * _vertices[i]->getCoords();
    }
    if (coords.size() == 2){
      _txtWriter.writeData("Coordinate", Eigen::Vector2d(coords));
    }
    else {
      _txtWriter.writeData("Coordinate", Eigen::Vector3d(coords));
    }
    // Export watch point data
    for (auto & elem : _dataToExport){
      if (elem->getDimensions() > 1){
        Eigen::VectorXd toExport = Eigen::VectorXd::Zero(_mesh->getDimensions());
        getValue(toExport, elem);
        if (coords.size() == 2){
          _txtWriter.writeData(elem->getName(), Eigen::Vector2d(toExport));
        }
        else {
          _txtWriter.writeData(elem->getName(), Eigen::Vector3d(toExport));
        }

      }
      else {
        double valueToExport = 0.0;
        getValue ( valueToExport, elem );
        _txtWriter.writeData ( elem->getName(), valueToExport );
      }
    }
  }
}

void WatchPoint:: getValue
(
  Eigen::VectorXd&  value,
  mesh::PtrData&    data )
{
  int dim = _mesh->getDimensions();
  Eigen::VectorXd temp(dim);
  size_t index = 0;
  const Eigen::VectorXd& values = data->values();
  for (mesh::Vertex* vertex : _vertices ) {
    int offset = vertex->getID()*dim;
    for ( int i=0; i < dim; i++ ){
      temp[i] = values[offset + i];
    }
    temp *= _weights[index];
    value += temp;
    index ++;
  }
}

void WatchPoint:: getValue
(
  double&        value,
  mesh::PtrData& data  )
{
  size_t index = 0;
  const Eigen::VectorXd& values = data->values();
  for (mesh::Vertex* vertex : _vertices ) {
    value += _weights[index] * values[vertex->getID()];
    index ++;
  }
}

}} // namespace precice, impl

