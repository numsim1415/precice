#ifndef PRECICE_NO_MPI
#include "mpi.h"
#endif
#include "tarch/multicore/configurations/CoreConfiguration.h"
#include "utils/assertion.hpp"


precice::logging::Logger tarch::multicore::configurations::CoreConfiguration::_log( "tarch::multicore::configurations::CoreConfiguration" );


tarch::multicore::configurations::CoreConfiguration::CoreConfiguration():
  _numberOfThreads(-1),
  _hasParsed(false) {
}


tarch::multicore::configurations::CoreConfiguration::~CoreConfiguration() {
}


std::string tarch::multicore::configurations::CoreConfiguration::getTag() const {
  return "multicore";
}


void tarch::multicore::configurations::CoreConfiguration::parseSubtag( tarch::irr::io::IrrXMLReader* xmlReader ) {
  _hasParsed = true;

  if ( xmlReader->getAttributeValue("threads")==0 ) {
    preciceWarning("parseSubtag(...)", "attribute \"threads\" missing within tag <" + getTag() + ">");
    _numberOfThreads = -1;
  }
  else if ( std::string("*") == std::string(xmlReader->getAttributeValue("threads")) ) {
    #ifdef SharedCobra
    preciceWarning("parseSubtag(...)", "Cobra does not support a default number of threads. Please specify valid number of threads");
    _numberOfThreads = -1;
    #else
    DEBUG("parseSubtag(...)", "use default number of threads");
    _numberOfThreads = 0;
    #endif
  }
  else {
    _numberOfThreads = xmlReader->getAttributeValueAsInt( "threads");
  }
}


bool tarch::multicore::configurations::CoreConfiguration::isValid() const {
  if (!_hasParsed) {
    assertion( _numberOfThreads<0 );
    preciceWarning( "isValid()", "tag <" + getTag() + "> is missing" );
  }
  return _numberOfThreads >=0;
}


void tarch::multicore::configurations::CoreConfiguration::toXML(std::ostream& out) const {
  out << "<!--" << std::endl
      << "  Configures the multithreading. This tag has only one attribute and" << std::endl
      << "  the attribute's value is either a natural number of a start to denote" << std::endl
      << "  that the system should use the default settings." << std::endl
      << "  -->" << std::endl;
  if (_numberOfThreads>0) {
    out << "<" << getTag() << " threads=\"" << _numberOfThreads << "\" />";
  }
  else {
    out << "<" << getTag() << " threads=\"*\" />";
  }
}


int tarch::multicore::configurations::CoreConfiguration::getNumberOfThreads() const {
  assertion( isValid() );
  return _numberOfThreads;
}
