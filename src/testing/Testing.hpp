#pragma once

#include <boost/test/unit_test.hpp>

#include "utils/Parallel.hpp"

namespace precice { namespace testing {

namespace bt = boost::unit_test;

/// Boost.Test decorator that makes the test run only on specfic ranks
class OnRanks : public bt::decorator::base
{
public:

  explicit OnRanks(const std::vector<int> & ranks) :
    _ranks(ranks)
  {}

private:
  
  virtual void apply(bt::test_unit& tu)
  {
    size_t rank = precice::utils::Parallel::getProcessRank();
    size_t size = precice::utils::Parallel::getCommunicatorSize();

    if (_ranks.size() > size) {
      tu.p_default_status.value = bt::test_case::RS_DISABLED;
      return;
    }
    
    if (std::find(_ranks.begin(), _ranks.end(), rank) == _ranks.end()) {
      tu.p_default_status.value = bt::test_case::RS_DISABLED;
      return;
    }
  }

  virtual bt::decorator::base_ptr clone() const
  {
    return bt::decorator::base_ptr(new OnRanks(_ranks));
  }

  std::vector<int> _ranks;
  
};

/// Boost.Test decorator that makes the test run only on the master
class OnMaster : public OnRanks
{
public:
  explicit OnMaster() :
    OnRanks({0})
  {}
};


#ifdef PRECICE_NO_PETSC
#define TEST_ON_MASTER do {                                     \
    if ( precice::utils::Parallel::getProcessRank() != 0 )      \
      return;                                                   \
  } while (false)

#else

#define TEST_ON_MASTER do {                                     \
    PETSC_COMM_WORLD = PETSC_COMM_SELF;                         \
    if ( precice::utils::Parallel::getProcessRank() != 0 )      \
      return;                                                   \
  } while (false)
#endif

#define TEST_ON_FOUR do {                                               \
    if (Par::getCommunicatorSize() > 3) {                               \
      MPI_Comm comm = utils::Parallel::getRestrictedCommunicator( {0, 1, 2, 3} ); \
      if (utils::Parallel::getProcessRank() <= 3)                       \
        utils::Parallel::setGlobalCommunicator(comm);                   \
      else                                                              \
        return;                                                         \
    }                                                                   \
  } while (false)

}}

