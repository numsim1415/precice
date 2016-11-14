#include <boost/test/unit_test.hpp>

#include "utils/Parallel.hpp"

namespace precice { namespace testing {

namespace bt = boost::unit_test;

/// Boost.Test decorator that makes the test run only on the master
class MasterOnly : public bt::decorator::base {
private:
  
  virtual void apply(bt::test_unit& tu)
  {
    if (precice::utils::Parallel::getProcessRank() != 0)
      tu.p_default_status.value = bt::test_case::RS_DISABLED;
  }

  virtual bt::decorator::base_ptr clone() const
  {
    return bt::decorator::base_ptr(new MasterOnly());
  }

};

/// Boost.Test decorator that makes the test run only on specfic ranks
class RanksOnly : public boost::unit_test::decorator::base {
public:

  explicit RanksOnly(const std::vector<int> & ranks) :
    _ranks(ranks)
  {}

private:
  
  virtual void apply(boost::unit_test::test_unit& tu)
  {
    int rank = precice::utils::Parallel::getProcessRank();
    
    if (std::find(_ranks.begin(), _ranks.end(), rank) == _ranks.end()) {
      tu.p_default_status.value = boost::unit_test::test_case::RS_DISABLED;
    }
  }

  virtual boost::unit_test::decorator::base_ptr clone() const
  {
    return boost::unit_test::decorator::base_ptr(new RanksOnly(_ranks));
  }

  std::vector<int> _ranks;
  
};


}}

