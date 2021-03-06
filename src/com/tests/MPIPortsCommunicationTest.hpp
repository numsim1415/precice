
#ifndef PRECICE_NO_MPI

#ifndef PRECICE_COM_TESTS_COMMUNICATIONMPIPORTSTEST_HPP_
#define PRECICE_COM_TESTS_COMMUNICATIONMPIPORTSTEST_HPP_

#include "tarch/tests/TestCase.h"
#include "logging/Logger.hpp"

namespace precice {
namespace com {
namespace tests {

/**
 * @brief Provides tests for class MPIPortsCommunication.
 */
class MPIPortsCommunicationTest : public tarch::tests::TestCase
{
public:

  /**
   * @brief Constructor.
   */
  MPIPortsCommunicationTest();

  /**
   * @brief Destructor, empty.
   */
  virtual ~MPIPortsCommunicationTest() {}

  /**
   * @brief Empty.
   */
  virtual void setUp() {}

  /**
   * @brief Runs all tests.
   */
  virtual void run();

private:

  // @brief Logging device.
  static logging::Logger _log;

# ifndef PRECICE_NO_MPI

  void testSendReceiveTwoProcesses();

# endif // not PRECICE_NO_MPI
};

}}} // namespace precice, com, tests

#endif /* PRECICE_COM_TESTS_COMMUNICATIONMPIPORTSTEST_HPP_ */

#endif // not PRECICE_NO_MPI
