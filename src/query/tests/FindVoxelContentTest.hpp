#ifndef PRECICE_QUERY_FINDVOXELCONTENTTEST_HPP_
#define PRECICE_QUERY_FINDVOXELCONTENTTEST_HPP_

#include "tarch/tests/TestCase.h"
#include "logging/Logger.hpp"
#include "utils/Dimensions.hpp"

namespace precice {
namespace query {
namespace tests {

/**
 * @brief Provides tests for class FindVoxelContent.
 */
class FindVoxelContentTest : public tarch::tests::TestCase
{
public:

  /**
   * @brief Constructor.
   */
	FindVoxelContentTest();

	/**
	 * @brief Preparations for tests, empty.
	 */
	virtual void setUp() {}

	/**
	 * @brief Runs all tests.
	 */
	virtual void run();

private:

	// @brief Logging device.
  static logging::Logger _log;

  /**
   * @brief Calls performTestVertices() with different parameters.
   */
  void testVertices();

  /**
   * @brief Performs vertex tests paramterized to one dimension.
   *
   * @param[in] dimension Coordinate dimension varied in the tests.
   * @param[in] positive  Sign of coordinates used (i.e. side of mesh queried).
   * @param[in] offset Offset of dimensions not varied (-1 < offset < 1).
   */
  void performTestVertices (
    int                     testDim,
    bool                    positive,
    const Eigen::VectorXd&  offset );

  /**
   * @brief Calls performTestEdges() with different parameters.
   */
  void testEdges();

  /**
   * @brief Performs edge tests parametrized to one dimension.
   *
   * @param[in] dimension Coordinate dimension varied in the tests.
   * @param[in] positive Sign of coordinates used (i.e. side of mesh queried).
   * @param[in] offset Offset of dimensions not varied (-1 < offset < 1).
   */
  void performTestEdges (
    int                     testDim,
    bool                    positive,
    const Eigen::VectorXd&  offset );

  void testZeroVoxel();

  void testTriangles();

  void performTestTriangles (
    int  testDim,
    int  secondDimension,
    bool positive );

  void testCompletelyInsideTriangles();

  void testCompletelyOutsideTriangles();

  void testIntersectingTriangles();

  void testTouchingTriangles();

  void testQueryCube();
};

}}} // namespace precice, query, tests

#endif /* PRECICE_QUERY_FINDVOXELCONTENTTEST_HPP_ */
