#ifndef PRECICE_SPACETREE_STATICOCTREETEST_HPP_
#define PRECICE_SPACETREE_STATICOCTREETEST_HPP_

#include "tarch/tests/TestCase.h"
#include "logging/Logger.hpp"
#include "spacetree/tests/SpacetreeTestScenarios.hpp"
#include "spacetree/StaticOctree.hpp"

namespace precice {
namespace spacetree {
namespace tests {

/**
 * @brief Provides test cases for class RegularSpacetree.
 */
class StaticOctreeTest : public tarch::tests::TestCase
{
public:

  /**
   * @brief Constructor.
   */
  //StaticOctreeTest();

  /**
   * @brief Empty.
   */
  virtual void setUp() {}

  /**
   * @brief Runs all tests.
   */
  virtual void run();

private:

  struct StaticOctreeFactory : public SpacetreeTestScenarios::SpacetreeFactory
  {
    virtual PtrSpacetree createSpacetree (
      const Eigen::VectorXd& offset,
      const Eigen::VectorXd& halflengths,
      double                 refinementLimit )
    {
      return PtrSpacetree(new StaticOctree(offset, halflengths[0], refinementLimit));
    }
  };

  // @brief Logging device.
  static logging::Logger _log;
};

}}} // namespace precice, spacetree, tests

#endif /* PRECICE_SPACETREE_STATICOCTREETEST_HPP_ */
