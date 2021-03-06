#include "FindClosestVertexVisitorTest.hpp"
#include "query/FindClosestVertex.hpp"
#include "mesh/Mesh.hpp"
#include "mesh/Vertex.hpp"
#include "utils/Parallel.hpp"
#include "math/math.hpp"
#include "tarch/tests/TestCaseFactory.h"
registerTest(precice::query::tests::FindClosestVertexVisitorTest)

namespace precice {
namespace query {
namespace tests {

logging::Logger FindClosestVertexVisitorTest::_log("precice::query::FindClosestVertexVisitorTest");

FindClosestVertexVisitorTest:: FindClosestVertexVisitorTest ()
:
  tarch::tests::TestCase ("query::FindClosestVertexVisitorTest")
{}

void FindClosestVertexVisitorTest:: run ()
{
  PRECICE_MASTER_ONLY {
    TRACE();
    mesh::Mesh mesh ( "Mesh", 2, false );
    mesh.createVertex ( Eigen::Vector2d(0.0, 0.0) );
    mesh.createVertex ( Eigen::Vector2d(0.0, 5.0) );
    FindClosestVertex find ( Eigen::Vector2d(1, 0) );
    bool found = find ( mesh );
    validate ( found );
    mesh::Vertex& closestVertex = find.getClosestVertex();
    validate ( math::equals(closestVertex.getCoords(), Eigen::Vector2d(0,0)) );
    double distance = find.getEuclidianDistance ();
    validateEquals (distance, 1.0);
  }
}

}}} // namespace precice, query, tests
