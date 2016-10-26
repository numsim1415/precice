#include "ComputeCurvatureAction.hpp"
#include "utils/Globals.hpp"
#include "utils/Dimensions.hpp"
#include "mesh/Mesh.hpp"
#include "mesh/Triangle.hpp"
#include "mesh/Edge.hpp"
#include "mesh/Vertex.hpp"
#include "utils/Globals.hpp"
#include <Eigen/Core>

namespace precice {
namespace action {

logging::Logger ComputeCurvatureAction::
  _log ( "precice::action::ComputeCurvatureAction" );

ComputeCurvatureAction:: ComputeCurvatureAction
(
  Timing               timing,
  int                  dataID,
  const mesh::PtrMesh& mesh )
:
  Action(timing, mesh),
  _data(mesh->data(dataID))
{}

void ComputeCurvatureAction:: performAction
(
  double time,
  double dt,
  double computedPartFullDt,
  double fullDt )
{
  TRACE();
  auto& dataValues = _data->values();

  if ( getMesh()->getDimensions() == 2 ){
    dataValues = Eigen::VectorXd::Zero(dataValues.size());
    //assign(dataValues) = 0.0;
    Eigen::Vector2d tangent;
    for (mesh::Edge & edge : getMesh()->edges()) {
      mesh::Vertex& v0 = edge.vertex(0);
      mesh::Vertex& v1 = edge.vertex(1);
      tangent = v1.getCoords();
      tangent -= v0.getCoords();
      tangent /= tangent.norm();
      for(int d=0; d < 2; d++) {
        dataValues[v0.getID()*2 + d] += tangent[d];
        dataValues[v1.getID()*2 + d] -= tangent[d];
      }
    }
  }
  else {
    assertion ( getMesh()->getDimensions() == 3, getMesh()->getDimensions() );
    for (int i=0; i < dataValues.size(); i++) {
      dataValues[i] = 0.0;
    }
    Eigen::Vector3d normal;
    Eigen::Vector3d edge;
    Eigen::Vector3d contribution;

    for (mesh::Triangle& tri : getMesh()->triangles()) {
      normal = tri.getNormal();
      for (int i=0; i < 3; i++) {
        mesh::Vertex& v0 = tri.vertex(i);
        mesh::Vertex& v1 = tri.vertex((i+1) % 3);
        edge = v1.getCoords();
        edge -= v0.getCoords();
        contribution = edge.cross(normal);
        for ( int d=0; d < 3; d++ ) {
          dataValues[v0.getID() * 3 + d] -= 0.25 * contribution[d];
          dataValues[v1.getID() * 3 + d] -= 0.25 * contribution[d];
        }
      }
    }
  }
}

}} // namespace precice, action
