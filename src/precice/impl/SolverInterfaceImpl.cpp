#include "SolverInterfaceImpl.hpp"
#include "precice/impl/Participant.hpp"
#include "precice/impl/WatchPoint.hpp"
#include "precice/impl/RequestManager.hpp"
#include "precice/config/Configuration.hpp"
#include "precice/config/SolverInterfaceConfiguration.hpp"
#include "precice/config/ParticipantConfiguration.hpp"
#include "mesh/config/DataConfiguration.hpp"
#include "mesh/config/MeshConfiguration.hpp"
#include "mesh/Mesh.hpp"
#include "mesh/Group.hpp"
#include "mesh/PropertyContainer.hpp"
#include "mesh/Vertex.hpp"
#include "mesh/Edge.hpp"
#include "mesh/Triangle.hpp"
#include "mesh/Merge.hpp"
#include "io/ExportVTK.hpp"
#include "io/ExportVRML.hpp"
#include "io/ExportContext.hpp"
#include "io/SimulationStateIO.hpp"
#include "io/TXTWriter.hpp"
#include "io/TXTReader.hpp"
#include "query/FindClosest.hpp"
#include "query/FindVoxelContent.hpp"
#include "spacetree/config/SpacetreeConfiguration.hpp"
#include "spacetree/Spacetree.hpp"
#include "spacetree/ExportSpacetree.hpp"
#include "com/MPIPortsCommunication.hpp"
#include "com/Constants.hpp"
#include "com/Communication.hpp"
#include "com/MPIDirectCommunication.hpp"
#include "m2n/config/M2NConfiguration.hpp"
#include "m2n/M2N.hpp"
#include "m2n/PointToPointCommunication.hpp"
#include "geometry/config/GeometryConfiguration.hpp"
#include "geometry/Geometry.hpp"
#include "geometry/ImportGeometry.hpp"
#include "geometry/CommunicatedGeometry.hpp"
#include "geometry/impl/Decomposition.hpp"
#include "geometry/impl/PreFilterPostFilterDecomposition.hpp"
#include "geometry/impl/BroadcastFilterDecomposition.hpp"
#include "geometry/SolverGeometry.hpp"
#include "cplscheme/CouplingScheme.hpp"
#include "cplscheme/config/CouplingSchemeConfiguration.hpp"
#include "utils/EventTimings.hpp"
#include "utils/Globals.hpp"
#include "utils/SignalHandler.hpp"
#include "utils/Parallel.hpp"
#include "utils/Petsc.hpp"
#include "utils/MasterSlave.hpp"
#include "mapping/Mapping.hpp"
#include <set>
#include <cstring>
#include <Eigen/Dense>

#include <signal.h> // used for installing crash handler

#include "logging/Logger.hpp"
#include "logging/LogConfiguration.hpp"


using precice::utils::Event;
using precice::utils::EventRegistry;

namespace precice {

bool testMode = false;

namespace impl {

logging::Logger SolverInterfaceImpl::
  _log ("precice::impl::SolverInterfaceImpl");

SolverInterfaceImpl:: SolverInterfaceImpl
(
  const std::string& accessorName,
  int                accessorProcessRank,
  int                accessorCommunicatorSize,
  bool               serverMode )
:
  _accessorName(accessorName),
  _accessorProcessRank(accessorProcessRank),
  _accessorCommunicatorSize(accessorCommunicatorSize),
  _accessor(),
  _dimensions(0),
  _geometryMode(false),
  _restartMode(false),
  _serverMode(serverMode),
  _clientMode(false),
  _meshIDs(),
  _dataIDs(),
  _exportVTKNeighbors(),
  _m2ns(),
  _participants(),
  _checkpointTimestepInterval(-1),
  _checkpointFileName("precice_checkpoint_" + _accessorName),
  _numberAdvanceCalls(0),
  _requestManager(nullptr)
{
  CHECK(_accessorProcessRank >= 0, "Accessor process index has to be >= 0!");
  CHECK(_accessorCommunicatorSize >= 0, "Accessor process size has to be >= 0!");
  CHECK(_accessorProcessRank < _accessorCommunicatorSize,
        "Accessor process index has to be smaller than accessor process "
        << "size (given as " << _accessorProcessRank << ")!");

  precice::utils::EventRegistry::initialize();

  /* When precice stops abruptly, e.g. an external solver crashes, the
     SolverInterfaceImpl destructor is never called. Since we still want
     to print the timings, we install the signal handler here. */
  signal(SIGSEGV, precice::utils::terminationSignalHandler);
  signal(SIGABRT, precice::utils::terminationSignalHandler);
  signal(SIGTERM, precice::utils::terminationSignalHandler);
  // signal(SIGINT,  precice::utils::terminationSignalHandler);

  // precice::logging::setupLogging();
}

SolverInterfaceImpl:: ~SolverInterfaceImpl()
{
  TRACE();
  if (_requestManager != nullptr){
    delete _requestManager;
  }
}

void SolverInterfaceImpl:: configure
(
  const std::string& configurationFileName )
{
  TRACE(configurationFileName );
  mesh::Mesh::resetGeometryIDsGlobally();
  mesh::Data::resetDataCount();
  Participant::resetParticipantCount();

  config::Configuration config;
  utils::configure(config.getXMLTag(), configurationFileName);
  //preciceCheck ( config.isValid(), "configure()", "Invalid configuration file!" );

  configure(config.getSolverInterfaceConfiguration());
}

void SolverInterfaceImpl:: configure
(
  const config::SolverInterfaceConfiguration& config )
{
  TRACE();

  _dimensions = config.getDimensions();
  _geometryMode = config.isGeometryMode ();
  _restartMode = config.isRestartMode ();
  _accessor = determineAccessingParticipant(config);

  preciceCheck(not (_accessor->useServer() && _accessor->useMaster()),
                     "configure()", "You cannot use a server and a master.");
  preciceCheck(not (_restartMode && _accessor->useMaster()),"configure()",
                      "To restart while using a master is not yet supported");
  preciceCheck(_accessorCommunicatorSize==1 || _accessor->useMaster() || _accessor->useServer(),
                     "configure()", "A parallel participant needs either a master or a server communication configured");

  _clientMode = (not _serverMode) && _accessor->useServer();

  if(_accessor->useMaster()){
    utils::MasterSlave::configure(_accessorProcessRank, _accessorCommunicatorSize);
  }

  _participants = config.getParticipantConfiguration()->getParticipants();
  configureM2Ns(config.getM2NConfiguration());

  if (_serverMode){
    preciceInfo("configure()", "[PRECICE] Run in server mode");
  }
  if (_clientMode){
    preciceInfo("configure()", "[PRECICE] Run in client mode");
  }

  if (_geometryMode){
    preciceInfo("configure()", "[PRECICE] Run in geometry mode");
    preciceCheck(_participants.size() == 1, "configure()",
                 "Only one participant can be defined in geometry mode!");
    configureSolverGeometries(config.getM2NConfiguration());
  }
  else if (not _clientMode){
    preciceInfo("configure()", "[PRECICE] Run in coupling mode");
    preciceCheck(_participants.size() > 1,
                 "configure()", "At least two participants need to be defined!");
    configureSolverGeometries(config.getM2NConfiguration());
  }

  // Set coupling scheme. In geometry mode, an uncoupled scheme is automatically
  // created.
  cplscheme::PtrCouplingSchemeConfiguration cplSchemeConfig =
      config.getCouplingSchemeConfiguration();
  _couplingScheme = cplSchemeConfig->getCouplingScheme(_accessorName);

  if (_restartMode){
    _couplingScheme->requireAction(constants::actionReadSimulationCheckpoint());
  }

  if (_serverMode || _clientMode){
    com::Communication::SharedPointer com = _accessor->getClientServerCommunication();
    assertion(com.get() != nullptr);
    _requestManager = new RequestManager(_geometryMode, *this, com, _couplingScheme);
  }

  // Add meshIDs, data IDs, and spacetrees
  for (MeshContext* meshContext : _accessor->usedMeshContexts()) {
    const mesh::PtrMesh& mesh = meshContext->mesh;
    for (std::pair<std::string,int> nameID : mesh->getNameIDPairs()) {
      assertion(not utils::contained(nameID.first, _meshIDs));
      _meshIDs[nameID.first] = nameID.second;
    }
    assertion(_dataIDs.find(mesh->getID())==_dataIDs.end());
    _dataIDs[mesh->getID()] = std::map<std::string,int>();
    assertion(_dataIDs.find(mesh->getID())!=_dataIDs.end());
    for (const mesh::PtrData& data : mesh->data()) {
      assertion(_dataIDs[mesh->getID()].find(data->getName())==_dataIDs[mesh->getID()].end());
      _dataIDs[mesh->getID()][data->getName()] = data->getID();
    }
    std::string meshName = mesh->getName();
    mesh::PtrMeshConfiguration meshConfig = config.getMeshConfiguration();
    spacetree::PtrSpacetreeConfiguration spacetreeConfig = config.getSpacetreeConfiguration();
    if (meshConfig->doesMeshUseSpacetree(meshName)){
      std::string spacetreeName = meshConfig->getSpacetreeName(meshName);
      meshContext->spacetree = spacetreeConfig->getSpacetree(spacetreeName);
    }
  }

  int argc = 1;
  char* arg = new char[8];
  strcpy(arg, "precice");
  char** argv = &arg;
  utils::Parallel::initializeMPI(&argc, &argv);
  precice::logging::setMPIRank(utils::Parallel::getProcessRank());
  delete[] arg;

  // Setup communication to server
  if (_clientMode){
    initializeClientServerCommunication();
  }
  if (utils::MasterSlave::_masterMode || utils::MasterSlave::_slaveMode){
    initializeMasterSlaveCommunication();
  }
}

double SolverInterfaceImpl:: initialize()
{
  TRACE();
  Event e("initialize", not precice::testMode);

  m2n::PointToPointCommunication::ScopedSetEventNamePrefix ssenp(
      "initialize"
      "/");

  if (_clientMode){
    DEBUG("Request perform initializations");
    _requestManager->requestInitialize();
  }
  else {
    // Setup communication
    if (not _geometryMode){
      typedef std::map<std::string,M2NWrap>::value_type M2NPair;
      preciceInfo("initialize()", "Setting up master communication to coupling partner/s " );
      for (M2NPair& m2nPair : _m2ns) {
        m2n::M2N::SharedPointer& m2n = m2nPair.second.m2n;
        std::string localName = _accessorName;
        if (_serverMode) localName += "Server";
        std::string remoteName(m2nPair.first);
        preciceCheck(m2n.get() != nullptr, "initialize()",
                     "M2N communication from " << localName << " to participant "
                     << remoteName << " could not be created! Check compile "
                     "flags used!");
        if (m2nPair.second.isRequesting){
          m2n->requestMasterConnection(remoteName, localName);
        }
        else {
          m2n->acceptMasterConnection(localName, remoteName);
        }
      }
      preciceInfo("initialize()", "Coupling partner/s are connected " );
    }

    DEBUG("Perform initializations");

    //create geometry. we need to do this in two loops, to first communicate the mesh and later decompose it
    //originally this was done in one loop. this however gave deadlock if two meshes needed to be communicated cross-wise.
    //both loops need a different sorting

    // sort meshContexts by name, for communication in right order.
    std::sort (_accessor->usedMeshContexts().begin(), _accessor->usedMeshContexts().end(),
        []( MeshContext* lhs, const MeshContext* rhs) -> bool
        {
          return lhs->mesh->getName() < rhs->mesh->getName();
        } );

    for (MeshContext* meshContext : _accessor->usedMeshContexts()){
      prepareGeometry(*meshContext);
    }

    // now sort provided meshes up front, to have them ready for the decomposition
    std::sort (_accessor->usedMeshContexts().begin(), _accessor->usedMeshContexts().end(),
        []( MeshContext* lhs, const MeshContext* rhs) -> bool
        {
          if(lhs->provideMesh && not rhs->provideMesh){
            return true;
          }
          if(not lhs->provideMesh && rhs->provideMesh){
            return false;
          }
          return lhs->mesh->getName() < rhs->mesh->getName();
        } );

    for (MeshContext* meshContext : _accessor->usedMeshContexts()){
      createGeometry(*meshContext);
    }


    typedef std::map<std::string,M2NWrap>::value_type M2NPair;
    preciceInfo("initialize()", "Setting up slaves communication to coupling partner/s " );
    for (M2NPair& m2nPair : _m2ns) {
      m2n::M2N::SharedPointer& m2n = m2nPair.second.m2n;
      std::string localName = _accessorName;
      std::string remoteName(m2nPair.first);
      preciceCheck(m2n.get() != nullptr, "initialize()",
                   "Communication from " << localName << " to participant "
                   << remoteName << " could not be created! Check compile "
                   "flags used!");
      if (m2nPair.second.isRequesting){
        m2n->requestSlavesConnection(remoteName, localName);
      }
      else {
        m2n->acceptSlavesConnection(localName, remoteName);
      }
    }
    preciceInfo("initialize()", "Slaves are connected" );

    std::set<action::Action::Timing> timings;
    double dt = 0.0;

    for (PtrWatchPoint& watchPoint : _accessor->watchPoints()){
      watchPoint->initialize();
    }

    // Initialize coupling state
    double time = 0.0;
    int timestep = 1;

    if (_restartMode){
      preciceInfo("initialize()", "Reading simulation state for restart");
      io::SimulationStateIO stateIO(_checkpointFileName + "_simstate.txt");
      stateIO.readState(time, timestep, _numberAdvanceCalls);
    }

    _couplingScheme->initialize(time, timestep);

    if (_restartMode){
      preciceInfo("initialize()", "Reading coupling scheme state for restart");
      //io::TXTReader txtReader(_checkpointFileName + "_cplscheme.txt");
      _couplingScheme->importState(_checkpointFileName);
    }

    dt = _couplingScheme->getNextTimestepMaxLength();

    timings.insert(action::Action::ALWAYS_POST);

    if (_couplingScheme->hasDataBeenExchanged()){
      timings.insert(action::Action::ON_EXCHANGE_POST);
      mapReadData();
    }

    performDataActions(timings, 0.0, 0.0, 0.0, dt);

    preciceInfo("initialize()", _couplingScheme->printCouplingState());
  }
  return _couplingScheme->getNextTimestepMaxLength();
}

void SolverInterfaceImpl:: initializeData ()
{
  preciceTrace("initializeData()" );
  Event e("initializeData", not precice::testMode);

  m2n::PointToPointCommunication::ScopedSetEventNamePrefix ssenp(
      "initializeData"
      "/");

  preciceCheck(_couplingScheme->isInitialized(), "initializeData()",
               "initialize() has to be called before initializeData()");
  if (not _geometryMode){
    if (_clientMode){
      _requestManager->requestInitialzeData();
    }
    else {
      mapWrittenData();
      _couplingScheme->initializeData();
      double dt = _couplingScheme->getNextTimestepMaxLength();
      std::set<action::Action::Timing> timings;
      if (_couplingScheme->hasDataBeenExchanged()){
        timings.insert(action::Action::ON_EXCHANGE_POST);
        mapReadData();
      }
      performDataActions(timings, 0.0, 0.0, 0.0, dt);
      resetWrittenData();
      DEBUG("Plot output...");
      for (const io::ExportContext& context : _accessor->exportContexts()){
        if (context.timestepInterval != -1){
          std::ostringstream suffix;
          suffix << _accessorName << ".init";
          exportMesh(suffix.str());
          if (context.triggerSolverPlot){
            _couplingScheme->requireAction(constants::actionPlotOutput());
          }
        }
      }
    }
  }
}

double SolverInterfaceImpl:: advance
(
  double computedTimestepLength )
{
  TRACE(computedTimestepLength);

  Event e("advance", not precice::testMode);

  m2n::PointToPointCommunication::ScopedSetEventNamePrefix ssenp(
      "advance"
      "/");

  preciceCheck(_couplingScheme->isInitialized(), "advance()",
               "initialize() has to be called before advance()");
  _numberAdvanceCalls++;
  if (_clientMode){
    _requestManager->requestAdvance(computedTimestepLength);
  }
  else {
#   ifdef Debug
    if(utils::MasterSlave::_masterMode || utils::MasterSlave::_slaveMode){
      syncTimestep(computedTimestepLength);
    }
#   endif

    double timestepLength = 0.0; // Length of (full) current dt
    double timestepPart = 0.0;   // Length of computed part of (full) curr. dt
    double time = 0.0;


    // Update the coupling scheme time state. Necessary to get correct remainder.
    _couplingScheme->addComputedTime(computedTimestepLength);

    if (_geometryMode){
      timestepLength = computedTimestepLength;
      timestepPart = computedTimestepLength;
    }
    else {
      //double timestepLength = 0.0;
      if (_couplingScheme->hasTimestepLength()){
        timestepLength = _couplingScheme->getTimestepLength();
      }
      else {
        timestepLength = computedTimestepLength;
      }
      timestepPart = timestepLength - _couplingScheme->getThisTimestepRemainder();
    }
    time = _couplingScheme->getTime();


    mapWrittenData();

    std::set<action::Action::Timing> timings;

    timings.insert(action::Action::ALWAYS_PRIOR);
    if (_couplingScheme->willDataBeExchanged(0.0)){
      timings.insert(action::Action::ON_EXCHANGE_PRIOR);
    }
    performDataActions(timings, time, computedTimestepLength, timestepPart, timestepLength);

    DEBUG("Advancing coupling scheme");
    _couplingScheme->advance();

    timings.clear();
    timings.insert(action::Action::ALWAYS_POST);
    if (_couplingScheme->hasDataBeenExchanged()){
      timings.insert(action::Action::ON_EXCHANGE_POST);
    }
    if (_couplingScheme->isCouplingTimestepComplete()){
      timings.insert(action::Action::ON_TIMESTEP_COMPLETE_POST);
    }
    performDataActions(timings, time, computedTimestepLength, timestepPart, timestepLength);

    if (_couplingScheme->hasDataBeenExchanged()){
      mapReadData();
    }

    preciceInfo("advance()", _couplingScheme->printCouplingState());

    handleExports();

    // deactivated the reset of written data, as it deletes all data that is not communicated
    // within this cycle in the coupling data. This is not wanted forthe manifold mapping.
    //resetWrittenData();

  }
  return _couplingScheme->getNextTimestepMaxLength();
}

void SolverInterfaceImpl:: finalize()
{
  preciceTrace("finalize()");
  preciceCheck(_couplingScheme->isInitialized(), "finalize()",
               "initialize() has to be called before finalize()");
  _couplingScheme->finalize();
  _couplingScheme.reset();

  if (_clientMode){
    _requestManager->requestFinalize();
    _accessor->getClientServerCommunication()->closeConnection();
  }
  else {
    for (const io::ExportContext& context : _accessor->exportContexts()){
      if ( context.timestepInterval != -1 ){
        std::ostringstream suffix;
        suffix << _accessorName << ".final";
        exportMesh ( suffix.str() );
        if ( context.triggerSolverPlot ) {
          _couplingScheme->requireAction ( constants::actionPlotOutput() );
        }
      }
    }
    // Apply some final ping-pong to synch solver that run e.g. with a uni-directional coupling only
    // afterwards close connections
    std::string ping = "ping";
    std::string pong = "pong";
    for (auto &iter : _m2ns) {
      if( not utils::MasterSlave::_slaveMode){
        if(iter.second.isRequesting){
          iter.second.m2n->getMasterCommunication()->startSendPackage(0);
          iter.second.m2n->getMasterCommunication()->send(ping,0);
          iter.second.m2n->getMasterCommunication()->finishSendPackage();
          std::string receive = "init";
          iter.second.m2n->getMasterCommunication()->startReceivePackage(0);
          iter.second.m2n->getMasterCommunication()->receive(receive,0);
          iter.second.m2n->getMasterCommunication()->finishReceivePackage();
          assertion(receive==pong);
        }
        else{
          std::string receive = "init";
          iter.second.m2n->getMasterCommunication()->startReceivePackage(0);
          iter.second.m2n->getMasterCommunication()->receive(receive,0);
          iter.second.m2n->getMasterCommunication()->finishReceivePackage();
          assertion(receive==ping);
          iter.second.m2n->getMasterCommunication()->startSendPackage(0);
          iter.second.m2n->getMasterCommunication()->send(pong,0);
          iter.second.m2n->getMasterCommunication()->finishSendPackage();
        }
      }
      iter.second.m2n->closeConnection();
    }
  }
  if(utils::MasterSlave::_slaveMode || utils::MasterSlave::_masterMode){
    utils::MasterSlave::_communication->closeConnection();
    utils::MasterSlave::_communication = nullptr;
  }

  if(_serverMode){
    _accessor->getClientServerCommunication()->closeConnection();
  }

  // Stop and print Event logging
  precice::utils::EventRegistry::finalize();
  if (not precice::utils::MasterSlave::_slaveMode) {
    precice::utils::EventRegistry r;
    r.print();
    r.print("EventTimings.log", true);
  }

  // Tear down MPI and PETSc
  if (not precice::testMode && not _serverMode ) {
    utils::Petsc::finalize();
    utils::Parallel::finalizeMPI();
  }
  utils::Parallel::clearGroups();
}

int SolverInterfaceImpl:: getDimensions() const
{
  TRACE(_dimensions );
  return _dimensions;
}

bool SolverInterfaceImpl:: isCouplingOngoing()
{
  preciceTrace ( "isCouplingOngoing()" );
  return _couplingScheme->isCouplingOngoing();
}

bool SolverInterfaceImpl:: isReadDataAvailable()
{
  preciceTrace ( "isReadDataAvailable()" );
  return _couplingScheme->hasDataBeenExchanged();
}

bool SolverInterfaceImpl:: isWriteDataRequired
(
  double computedTimestepLength )
{
  preciceTrace ( "isWriteDataRequired()", computedTimestepLength );
  return _couplingScheme->willDataBeExchanged(computedTimestepLength);
}

bool SolverInterfaceImpl:: isTimestepComplete()
{
  preciceTrace ( "isCouplingTimestepComplete()" );
  return _couplingScheme->isCouplingTimestepComplete();
}

bool SolverInterfaceImpl:: isActionRequired
(
  const std::string& action )
{
  preciceTrace("isActionRequired()", action, _couplingScheme->isActionRequired(action));
  return _couplingScheme->isActionRequired(action);
}

void SolverInterfaceImpl:: fulfilledAction
(
  const std::string& action )
{
  preciceTrace ( "fulfilledAction()", action );
  if ( _clientMode ) {
    _requestManager->requestFulfilledAction(action);
  }
  _couplingScheme->performedAction(action);
}

bool SolverInterfaceImpl::hasToEvaluateSurrogateModel()
{
 // std::cout<<"_isCoarseModelOptimizationActive() = "<<_couplingScheme->isCoarseModelOptimizationActive();
  return _couplingScheme->isCoarseModelOptimizationActive();
}

bool SolverInterfaceImpl::hasToEvaluateFineModel()
{
  return not _couplingScheme->isCoarseModelOptimizationActive();
}

bool SolverInterfaceImpl:: hasMesh
(
  const std::string& meshName ) const
{
  preciceTrace ( "hasMesh()", meshName );
  return utils::contained ( meshName, _meshIDs );
}

int SolverInterfaceImpl:: getMeshID
(
  const std::string& meshName )
{
  preciceTrace ( "getMeshID()", meshName );
  preciceCheck( utils::contained(meshName, _meshIDs), "getMeshID()",
                "Mesh with name \""<< meshName << "\" is not defined!" );
  return _meshIDs[meshName];
}

std::set<int> SolverInterfaceImpl:: getMeshIDs()
{
  preciceTrace ( "getMeshIDs()" );
  std::set<int> ids;
  for (const impl::MeshContext* context : _accessor->usedMeshContexts()) {
    ids.insert ( context->mesh->getID() );
  }
  return ids;
}

bool SolverInterfaceImpl:: hasData
(
  const std::string& dataName, int meshID )
{
  preciceTrace ( "hasData()", dataName, meshID );
  preciceCheck ( _dataIDs.find(meshID)!=_dataIDs.end(), "hasData()",
                   "No mesh with meshID \"" << meshID << "\" is defined");
  std::map<std::string,int>& sub_dataIDs =  _dataIDs[meshID];
  return sub_dataIDs.find(dataName)!= sub_dataIDs.end();
}

int SolverInterfaceImpl:: getDataID
(
  const std::string& dataName, int meshID )
{
  preciceTrace ( "getDataID()", dataName, meshID );
  preciceCheck ( hasData(dataName, meshID), "getDataID()",
                 "Data with name \"" << dataName << "\" is not defined on mesh with ID \""
                 << meshID << "\".");
  return _dataIDs[meshID][dataName];
}

int SolverInterfaceImpl:: inquirePosition
(
  const double*        point,
  const std::set<int>& meshIDs )
{
  preciceTrace ( "inquirePosition()", point, meshIDs.size() );
  using namespace precice::constants;
  int pos = positionOutsideOfGeometry();
  Eigen::VectorXd searchPoint(_dimensions);
  for (int dim=0; dim<_dimensions; dim++) searchPoint[dim] = point[dim];
  if (_clientMode){
    pos = _requestManager->requestInquirePosition(searchPoint, meshIDs);
  }
  else {
    std::vector<int> markedContexts(_accessor->usedMeshContexts().size());
    selectInquiryMeshIDs(meshIDs, markedContexts);
    for (int i=0; i < (int)markedContexts.size(); i++){
      MeshContext* meshContext = _accessor->usedMeshContexts()[i];
      if (markedContexts[i] == markedSkip()){
        DEBUG("Skipping mesh " << meshContext->mesh->getName());
        continue;
      }
      int tempPos = -1;
      if (markedContexts[i] == markedQuerySpacetree()){
        assertion(meshContext->spacetree.use_count() > 0);
        tempPos = meshContext->spacetree->searchPosition(searchPoint);
      }
      else {
        assertion(markedContexts[i] == markedQueryDirectly(), markedContexts[i]);
        query::FindClosest findClosest(searchPoint);
        findClosest(*(meshContext->mesh));
        assertion(findClosest.hasFound());
        tempPos = positionOnGeometry();
        if (math::greater(findClosest.getClosest().distance, 0.0)){
          tempPos = positionOutsideOfGeometry();
        }
        else if (math::greater(0.0, findClosest.getClosest().distance)){
          tempPos = positionInsideOfGeometry();
        }
      }

      // Union logic for multiple geometries:
      if (pos != positionInsideOfGeometry()){
        if (tempPos == positionOutsideOfGeometry()){
          if (pos != positionOnGeometry()){
            pos = tempPos; // set outside of geometry
          }
        }
        else {
          pos = tempPos; // set inside or on geometry
        }
      }
    }
  }
  DEBUG("Return position = " << pos);
  return pos;
}

ClosestMesh SolverInterfaceImpl:: inquireClosestMesh
(
  const double*        point,
  const std::set<int>& meshIDs )
{
  preciceTrace("inquireClosestMesh()", point);
  ClosestMesh closestMesh(_dimensions);
  Eigen::VectorXd searchPoint(_dimensions);
  for (int dim=0; dim < _dimensions; dim++){
    searchPoint[dim] = point[dim];
  }
  if (_clientMode){
    _requestManager->requestInquireClosestMesh(searchPoint, meshIDs, closestMesh);
  }
  else {
    using namespace precice::constants;
    std::vector<int> markedContexts(_accessor->usedMeshContexts().size());
    selectInquiryMeshIDs(meshIDs, markedContexts);
    closestMesh.setPosition(positionOutsideOfGeometry());
    //for (MeshContext& meshContext : _accessor->usedMeshContexts()){
    for (int i=0; i < (int)markedContexts.size(); i++){
      MeshContext* meshContext = _accessor->usedMeshContexts()[i];
      if (markedContexts[i] == markedSkip()){
        DEBUG("Skipping mesh " << meshContext->mesh->getName());
        continue;
      }
      query::FindClosest findClosest(searchPoint);
      if (markedContexts[i] == markedQuerySpacetree()){
        assertion(meshContext->spacetree.get() != nullptr);
        meshContext->spacetree->searchDistance(findClosest);
      }
      else {
        assertion(markedContexts[i] == markedQueryDirectly(), markedContexts[i]);
        findClosest(*(meshContext->mesh));
      }
      assertion(findClosest.hasFound());
      const query::ClosestElement& element = findClosest.getClosest();
      if ( element.distance > math::NUMERICAL_ZERO_DIFFERENCE &&
           closestMesh.position() == positionOutsideOfGeometry() )
      {
        if ( closestMesh.distance() > element.distance ) {
          closestMesh.setDistanceVector ( element.vectorToElement.data() );
          closestMesh.meshIDs() = element.meshIDs;
        }
      }
      else if ( element.distance < - math::NUMERICAL_ZERO_DIFFERENCE ) {
        closestMesh.setPosition ( positionInsideOfGeometry() );
        if ( closestMesh.distance() > std::abs(element.distance) ) {
          closestMesh.setDistanceVector ( element.vectorToElement.data() );
          closestMesh.meshIDs() = element.meshIDs;
        }
      }
      else if ( closestMesh.position() != positionInsideOfGeometry() ){
        closestMesh.setPosition ( positionOnGeometry() );
        closestMesh.setDistanceVector ( element.vectorToElement.data() );
        closestMesh.meshIDs() = element.meshIDs;
      }
      //if ( _accessor->exportContext().plotNeighbors ){
      //  _exportVTKNeighbors.addNeighbors ( searchPoint, element );
      //}
    }
  }
  return closestMesh;
}

VoxelPosition SolverInterfaceImpl:: inquireVoxelPosition
(
  const double*        voxelCenter,
  const double*        voxelHalflengths,
  bool                 includeBoundaries,
  const std::set<int>& meshIDs )
{
  preciceTrace("inquireVoxelPosition()", voxelCenter, voxelHalflengths,
                includeBoundaries, meshIDs.size());

  using namespace precice::constants;
  Eigen::VectorXd center(_dimensions);
  Eigen::VectorXd halflengths(_dimensions);
  for (int dim=0; dim < _dimensions; dim++){
    center[dim] = voxelCenter[dim];
    halflengths[dim] = voxelHalflengths[dim];
  }
  DEBUG("center = " << center << ", h = " << halflengths);

  if (_clientMode){
    VoxelPosition pos;
    _requestManager->requestInquireVoxelPosition(center, halflengths, includeBoundaries, meshIDs, pos);
    return pos;
  }
  query::FindVoxelContent::BoundaryInclusion boundaryInclude;
  boundaryInclude = includeBoundaries
                    ? query::FindVoxelContent::INCLUDE_BOUNDARY
                    : query::FindVoxelContent::EXCLUDE_BOUNDARY;

  //VoxelPosition voxelPosition;
  int pos = positionOutsideOfGeometry();
  //mesh::Group* content = new mesh::Group();
  std::vector<int> containedMeshIDs;
//  for (MeshContext& meshContext : _accessor->usedMeshContexts()){
//    bool skip = not utils::contained(meshContext.mesh->getID(), meshIDs);
//    skip &= not meshIDs.empty();
//    if (skip){
//      DEBUG("Skipping mesh " << meshContext.mesh->getName());
//      continue;
//    }

  std::vector<int> markedContexts(_accessor->usedMeshContexts().size());
  selectInquiryMeshIDs(meshIDs, markedContexts);
    //for (MeshContext& meshContext : _accessor->usedMeshContexts()){
  for (int i=0; i < (int)markedContexts.size(); i++){
    MeshContext* meshContext = _accessor->usedMeshContexts()[i];
    if (markedContexts[i] == markedSkip()){
      DEBUG("Skipping mesh " << meshContext->mesh->getName());
      continue;
    }
    DEBUG("Query mesh \"" << meshContext->mesh->getName() << "\" with "
                 << meshContext->mesh->vertices().size() << " vertices");
    int oldPos = pos;
    query::FindVoxelContent findVoxel(center, halflengths, boundaryInclude);
    if (markedContexts[i] == markedQuerySpacetree()){
      assertion(meshContext->spacetree.get() != nullptr);
      DEBUG("Use spacetree for query");
      // Query first including voxel boundaries. This enables to directly
      // use cached information of spacetree cells, that do also include
      // objects on boundaries.
      pos = meshContext->spacetree->searchContent(findVoxel);

      // MERGING DISABLED!!!! CONTENT MIGHT CONTAIN DUPLICATED ELEMENTS
//      if (not findVoxel.content().empty()){
//        DEBUG ( "Merging found content of size = " << findVoxel.content().size() );
//        mesh::Merge mergeContent;
//        mergeContent ( findVoxel.content() );
//        //findContent.content() = mergeContent.content();
//        DEBUG ( "Merged size = " << mergeContent.content().size() );
//        content->add ( mergeContent.content() );
//      }
      //content->add(findVoxel.content());

//      if ( pos == Spacetree::ON_GEOMETRY ) {
//        query::FindVoxelContent findVoxel ( inquiryCenter, halfLengthVoxel,
//            query::FindVoxelContent::EXCLUDE_BOUNDARY );
//        findVoxel ( findVoxelInclude.content() );
//        if ( ! findVoxel.content().empty() ) {
//          content->add ( findVoxel.content() );
//        }
//      }
    }
    // The mesh does not have a spacetree
    else {
      DEBUG("Query mesh directly");
      assertion(markedContexts[i] == markedQueryDirectly(), markedContexts[i]);
      //query::FindVoxelContent findVoxel(center, halflengths, boundaryInclude);
      findVoxel(*meshContext->mesh);
      // If the voxel does have content
      if (not findVoxel.content().empty()){
        pos = positionOnGeometry();
        //content->add ( findVoxel.content() );
      }
      // If the voxel is empty and not inside for any other checked geometry
      else if (oldPos != positionInsideOfGeometry()){
        //DEBUG("Query found no objects and oldpos isnt't inside");
        query::FindClosest findClosest(center);
        findClosest(*(meshContext->mesh));
        assertion(findClosest.hasFound());
        const query::ClosestElement& closest = findClosest.getClosest();
        pos = closest.distance > 0 ? positionOutsideOfGeometry()
                                   : positionInsideOfGeometry();
      }
    }

    // Retrieve mesh IDs of contained elements
    if (not findVoxel.content().empty()){
      int geoID = mesh::PropertyContainer::INDEX_GEOMETRY_ID;
      std::vector<int> tempIDs;
      std::set<int> uniqueIDs;
      for (mesh::Vertex& vertex : findVoxel.content().vertices()) {
        vertex.getProperties(geoID, tempIDs);
        for (int id : tempIDs) {
          uniqueIDs.insert(id);
        }
        tempIDs.clear();
      }
      for (mesh::Edge& edge : findVoxel.content().edges()) {
        edge.getProperties(geoID, tempIDs);
        for (int id : tempIDs) {
          uniqueIDs.insert(id);
        }
        tempIDs.clear();
      }
      for (mesh::Triangle& triangle : findVoxel.content().triangles()) {
        triangle.getProperties(geoID, tempIDs);
        for (int id : tempIDs) {
          uniqueIDs.insert(id);
        }
        tempIDs.clear();
      }
      DEBUG("Query found objects, ids.size = " << uniqueIDs.size());
      for (int id : uniqueIDs) {
        if (not utils::contained(id, containedMeshIDs)){
          containedMeshIDs.push_back(id);
        }
      }
    }

    if (oldPos == positionInsideOfGeometry()){
      DEBUG("Since oldpos is inside, reset to inside");
      pos = positionInsideOfGeometry();
    }
    else if ((oldPos == positionOnGeometry())
             && (pos == positionOutsideOfGeometry()))
    {
      DEBUG ( "Since old pos is on and pos is outside, reset to on" );
      pos = positionOnGeometry();
    }
    DEBUG("pos = " << pos);
  }
  DEBUG("Return voxel position = " << pos << ", ids.size = " << containedMeshIDs.size());
  return VoxelPosition(pos, containedMeshIDs);
}


int SolverInterfaceImpl:: getMeshVertexSize
(
  int meshID )
{
  preciceTrace("getMeshVertexSize()", meshID);
  int size = 0;
  if (_clientMode){
    size = _requestManager->requestGetMeshVertexSize(meshID);
  }
  else {
    MeshContext& context = _accessor->meshContext(meshID);
    assertion(context.mesh.get() != nullptr);
    size = context.mesh->vertices().size();
  }
  DEBUG("return " << size);
  return size;
}

void SolverInterfaceImpl:: resetMesh
(
  int meshID )
{
  preciceTrace("resetMesh()", meshID);
  if (_clientMode){
    _requestManager->requestResetMesh(meshID);
  }
  else {
    impl::MeshContext& context = _accessor->meshContext(meshID);
    bool hasMapping = context.fromMappingContext.mapping.use_count() > 0
              || context.toMappingContext.mapping.use_count() > 0;
    bool isStationary =
          context.fromMappingContext.timing == mapping::MappingConfiguration::INITIAL &&
              context.toMappingContext.timing == mapping::MappingConfiguration::INITIAL;

    preciceCheck(!isStationary, "resetMesh()", "A mesh with only initial mappings"
              << " must not be reseted");
    preciceCheck(hasMapping, "resetMesh()", "A mesh with no mappings"
                << " must not be reseted");

    DEBUG ( "Clear mesh positions for mesh \"" << context.mesh->getName() << "\"" );
    context.mesh->clear ();
  }
}

int SolverInterfaceImpl:: setMeshVertex
(
  int           meshID,
  const double* position )
{
  preciceTrace ( "setMeshVertex()", meshID );
  Eigen::VectorXd internalPosition(_dimensions);
  for ( int dim=0; dim < _dimensions; dim++ ){
    internalPosition[dim] = position[dim];
  }
  DEBUG("Position = " << internalPosition);
  int index = -1;
  if ( _clientMode ){
    index = _requestManager->requestSetMeshVertex ( meshID, internalPosition );
  }
  else {
    MeshContext& context = _accessor->meshContext(meshID);
    mesh::PtrMesh mesh(context.mesh);
    DEBUG("MeshRequirement: " << context.meshRequirement);
    index = mesh->createVertex(internalPosition).getID();
    mesh->allocateDataValues();
  }
  return index;
}

void SolverInterfaceImpl:: setMeshVertices
(
  int     meshID,
  int     size,
  double* positions,
  int*    ids )
{
  preciceTrace("setMeshVertices()", meshID, size);
  if (_clientMode){
    _requestManager->requestSetMeshVertices(meshID, size, positions, ids);
  }
  else { //couplingMode
    MeshContext& context = _accessor->meshContext(meshID);
    mesh::PtrMesh mesh(context.mesh);
    Eigen::VectorXd internalPosition(_dimensions);
    DEBUG("Set positions");
    for (int i=0; i < size; i++){
      for (int dim=0; dim < _dimensions; dim++){
        internalPosition[dim] = positions[i*_dimensions + dim];
      }
      ids[i] = mesh->createVertex(internalPosition).getID();
    }
    mesh->allocateDataValues();
  }
}

void SolverInterfaceImpl:: getMeshVertices
(
  int     meshID,
  size_t  size,
  int*    ids,
  double* positions )
{
  preciceTrace("getMeshVertices()", meshID, size);
  if (_clientMode){
    _requestManager->requestGetMeshVertices(meshID, size, ids, positions);
  }
  else {
    MeshContext& context = _accessor->meshContext(meshID);
    mesh::PtrMesh mesh(context.mesh);
    Eigen::VectorXd internalPosition(_dimensions);
    DEBUG("Get positions");
    assertion(mesh->vertices().size() <= size, mesh->vertices().size(), size);
    for (size_t i=0; i < size; i++){
      size_t id = ids[i];
      assertion(id < mesh->vertices().size(), mesh->vertices().size(), id);
      internalPosition = mesh->vertices()[id].getCoords();
      for (int dim=0; dim < _dimensions; dim++){
        positions[id*_dimensions + dim] = internalPosition[dim];
      }
    }
  }
}

void SolverInterfaceImpl:: getMeshVertexIDsFromPositions (
  int     meshID,
  size_t  size,
  double* positions,
  int*    ids )
{
  preciceTrace("getMeshVertexIDsFromPositions()", meshID, size);
  if (_clientMode){
    _requestManager->requestGetMeshVertexIDsFromPositions(meshID, size, positions, ids);
  }
  else {
    MeshContext& context = _accessor->meshContext(meshID);
    mesh::PtrMesh mesh(context.mesh);
    DEBUG("Get ids");
    Eigen::VectorXd internalPosition(_dimensions);
    Eigen::VectorXd position(_dimensions);
    assertion(mesh->vertices().size() <= size, mesh->vertices().size(), size);
    for (size_t i=0; i < size; i++){
      for (int dim=0; dim < _dimensions; dim++){
        position[dim] = positions[i*_dimensions+dim];
      }
      size_t j=0;
      for (j=0; j < mesh->vertices().size(); j++){
        internalPosition = mesh->vertices()[j].getCoords();
        if (math::equals(internalPosition, position)){
          ids[i] = j;
          break;
        }
      }
      preciceCheck(j < mesh->vertices().size(), "getMeshVertexIDsFromPositions()",
                   "Position " << i << "=" << position << " unknown!");
    }
  }
}



int SolverInterfaceImpl:: setMeshEdge
(
  int meshID,
  int firstVertexID,
  int secondVertexID )
{
  preciceTrace ( "setMeshEdge()", meshID, firstVertexID, secondVertexID );
  if (_restartMode){
    DEBUG("Ignoring edge, since restart mode is active");
    return -1;
  }
  if ( _clientMode ){
    return _requestManager->requestSetMeshEdge ( meshID, firstVertexID, secondVertexID );
  }
  else {
    MeshContext& context = _accessor->meshContext(meshID);
    if ( context.meshRequirement == mapping::Mapping::FULL ){
      DEBUG("Full mesh required.");
      mesh::PtrMesh& mesh = context.mesh;
      assertion(firstVertexID >= 0, firstVertexID);
      assertion(secondVertexID >= 0, secondVertexID);
      assertion(firstVertexID < (int)mesh->vertices().size(),
                 firstVertexID, mesh->vertices().size());
      assertion(secondVertexID < (int)mesh->vertices().size(),
                 secondVertexID, mesh->vertices().size());
      mesh::Vertex& v0 = mesh->vertices()[firstVertexID];
      mesh::Vertex& v1 = mesh->vertices()[secondVertexID];
      return mesh->createEdge(v0, v1).getID ();
    }
  }
  return -1;
}

void SolverInterfaceImpl:: setMeshTriangle
(
  int meshID,
  int firstEdgeID,
  int secondEdgeID,
  int thirdEdgeID )
{
  preciceTrace ( "setMeshTriangle()", meshID, firstEdgeID,
                  secondEdgeID, thirdEdgeID );
  if (_restartMode){
    DEBUG("Ignoring triangle, since restart mode is active");
    return;
  }
  if ( _clientMode ){
    _requestManager->requestSetMeshTriangle ( meshID, firstEdgeID, secondEdgeID, thirdEdgeID );
  }
  else {
    MeshContext& context = _accessor->meshContext(meshID);
    if ( context.meshRequirement == mapping::Mapping::FULL ){
      mesh::PtrMesh& mesh = context.mesh;
      assertion ( firstEdgeID >= 0 );
      assertion ( secondEdgeID >= 0 );
      assertion ( thirdEdgeID >= 0 );
      assertion ( (int)mesh->edges().size() > firstEdgeID );
      assertion ( (int)mesh->edges().size() > secondEdgeID );
      assertion ( (int)mesh->edges().size() > thirdEdgeID );
      mesh::Edge& e0 = mesh->edges()[firstEdgeID];
      mesh::Edge& e1 = mesh->edges()[secondEdgeID];
      mesh::Edge& e2 = mesh->edges()[thirdEdgeID];
      mesh->createTriangle ( e0, e1, e2 );
    }
  }
}

void SolverInterfaceImpl:: setMeshTriangleWithEdges
(
  int meshID,
  int firstVertexID,
  int secondVertexID,
  int thirdVertexID )
{
  preciceTrace("setMeshTriangleWithEdges()", meshID, firstVertexID,
                secondVertexID, thirdVertexID);
  if (_clientMode){
    _requestManager->requestSetMeshTriangleWithEdges(meshID,
                                                     firstVertexID,
                                                     secondVertexID,
                                                     thirdVertexID);
    return;
  }
  MeshContext& context = _accessor->meshContext(meshID);
  if (context.meshRequirement == mapping::Mapping::FULL){
    mesh::PtrMesh& mesh = context.mesh;
    assertion(firstVertexID >= 0, firstVertexID);
    assertion(secondVertexID >= 0, secondVertexID);
    assertion(thirdVertexID >= 0, thirdVertexID);
    assertion((int)mesh->vertices().size() > firstVertexID,
                mesh->vertices().size(), firstVertexID);
    assertion((int)mesh->vertices().size() > secondVertexID,
                mesh->vertices().size(), secondVertexID);
    assertion((int)mesh->vertices().size() > thirdVertexID,
                 mesh->vertices().size(), thirdVertexID);
    mesh::Vertex* vertices[3];
    vertices[0] = &mesh->vertices()[firstVertexID];
    vertices[1] = &mesh->vertices()[secondVertexID];
    vertices[2] = &mesh->vertices()[thirdVertexID];
    mesh::Edge* edges[3];
    edges[0] = nullptr;
    edges[1] = nullptr;
    edges[2] = nullptr;
    for (mesh::Edge& edge : mesh->edges()) {
      // Check edge 0
      bool foundEdge = edge.vertex(0).getID() == vertices[0]->getID();
      foundEdge &= edge.vertex(1).getID() == vertices[1]->getID();
      if (foundEdge){
        edges[0] = &edge;
        continue;
      }
      foundEdge = edge.vertex(0).getID() == vertices[1]->getID();
      foundEdge &= edge.vertex(1).getID() == vertices[0]->getID();
      if (foundEdge){
        edges[0] = &edge;
        continue;
      }

      // Check edge 1
      foundEdge = edge.vertex(0).getID() == vertices[1]->getID();
      foundEdge &= edge.vertex(1).getID() == vertices[2]->getID();
      if (foundEdge){
        edges[1] = &edge;
        continue;
      }
      foundEdge = edge.vertex(0).getID() == vertices[2]->getID();
      foundEdge &= edge.vertex(1).getID() == vertices[1]->getID();
      if (foundEdge){
        edges[1] = &edge;
        continue;
      }

      // Check edge 2
      foundEdge = edge.vertex(0).getID() == vertices[2]->getID();
      foundEdge &= edge.vertex(1).getID() == vertices[0]->getID();
      if (foundEdge){
        edges[2] = &edge;
        continue;
      }
      foundEdge = edge.vertex(0).getID() == vertices[0]->getID();
      foundEdge &= edge.vertex(1).getID() == vertices[2]->getID();
      if (foundEdge){
        edges[2] = &edge;
        continue;
      }
    }
    // Create missing edges
    if (edges[0] == nullptr){
      edges[0] = & mesh->createEdge(*vertices[0], *vertices[1]);
    }
    if (edges[1] == nullptr){
      edges[1] = & mesh->createEdge(*vertices[1], *vertices[2]);
    }
    if (edges[2] == nullptr){
      edges[2] = & mesh->createEdge(*vertices[2], *vertices[0]);
    }

    mesh->createTriangle(*edges[0], *edges[1], *edges[2]);
  }
}

void SolverInterfaceImpl:: setMeshQuad
(
  int meshID,
  int firstEdgeID,
  int secondEdgeID,
  int thirdEdgeID,
  int fourthEdgeID )
{
  preciceTrace("setMeshQuad()", meshID, firstEdgeID, secondEdgeID, thirdEdgeID,
                fourthEdgeID);
  if (_restartMode){
    DEBUG("Ignoring quad, since restart mode is active");
    return;
  }
  if (_clientMode){
    _requestManager->requestSetMeshQuad(meshID, firstEdgeID, secondEdgeID,
                                        thirdEdgeID, fourthEdgeID);
  }
  else {
    MeshContext& context = _accessor->meshContext(meshID);
    if (context.meshRequirement == mapping::Mapping::FULL){
      mesh::PtrMesh& mesh = context.mesh;
      assertion(firstEdgeID >= 0);
      assertion(secondEdgeID >= 0);
      assertion(thirdEdgeID >= 0);
      assertion(fourthEdgeID >= 0);
      assertion((int)mesh->edges().size() > firstEdgeID);
      assertion((int)mesh->edges().size() > secondEdgeID);
      assertion((int)mesh->edges().size() > thirdEdgeID);
      assertion((int)mesh->quads().size() > fourthEdgeID);
      mesh::Edge& e0 = mesh->edges()[firstEdgeID];
      mesh::Edge& e1 = mesh->edges()[secondEdgeID];
      mesh::Edge& e2 = mesh->edges()[thirdEdgeID];
      mesh::Edge& e3 = mesh->edges()[fourthEdgeID];
      mesh->createQuad(e0, e1, e2, e3);
    }
  }
}

void SolverInterfaceImpl:: setMeshQuadWithEdges
(
  int meshID,
  int firstVertexID,
  int secondVertexID,
  int thirdVertexID,
  int fourthVertexID )
{
  preciceTrace("setMeshQuadWithEdges()", meshID, firstVertexID,
                secondVertexID, thirdVertexID, fourthVertexID);
  if (_clientMode){
    _requestManager->requestSetMeshQuadWithEdges(
        meshID, firstVertexID, secondVertexID, thirdVertexID, fourthVertexID);
    return;
  }
  MeshContext& context = _accessor->meshContext(meshID);
  if (context.meshRequirement == mapping::Mapping::FULL){
    mesh::PtrMesh& mesh = context.mesh;
    assertion(firstVertexID >= 0, firstVertexID);
    assertion(secondVertexID >= 0, secondVertexID);
    assertion(thirdVertexID >= 0, thirdVertexID);
    assertion(fourthVertexID >= 0, fourthVertexID);
    assertion((int)mesh->vertices().size() > firstVertexID,
                 mesh->vertices().size(), firstVertexID);
    assertion((int)mesh->vertices().size() > secondVertexID,
                 mesh->vertices().size(), secondVertexID);
    assertion((int)mesh->vertices().size() > thirdVertexID,
                 mesh->vertices().size(), thirdVertexID);
    assertion((int)mesh->vertices().size() > fourthVertexID,
                 mesh->vertices().size(), fourthVertexID);
    mesh::Vertex* vertices[4];
    vertices[0] = &mesh->vertices()[firstVertexID];
    vertices[1] = &mesh->vertices()[secondVertexID];
    vertices[2] = &mesh->vertices()[thirdVertexID];
    vertices[3] = &mesh->vertices()[fourthVertexID];
    mesh::Edge* edges[4];
    edges[0] = nullptr;
    edges[1] = nullptr;
    edges[2] = nullptr;
    edges[3] = nullptr;
    for (mesh::Edge& edge : mesh->edges()) {
      // Check edge 0
      bool foundEdge = edge.vertex(0).getID() == vertices[0]->getID();
      foundEdge &= edge.vertex(1).getID() == vertices[1]->getID();
      if ( foundEdge ){
        edges[0] = &edge;
        continue;
      }
      foundEdge = edge.vertex(0).getID() == vertices[1]->getID();
      foundEdge &= edge.vertex(1).getID() == vertices[0]->getID();
      if (foundEdge){
        edges[0] = &edge;
        continue;
      }

      // Check edge 1
      foundEdge = edge.vertex(0).getID() == vertices[1]->getID();
      foundEdge &= edge.vertex(1).getID() == vertices[2]->getID();
      if ( foundEdge ){
        edges[1] = &edge;
        continue;
      }
      foundEdge = edge.vertex(0).getID() == vertices[2]->getID();
      foundEdge &= edge.vertex(1).getID() == vertices[1]->getID();
      if ( foundEdge ){
        edges[1] = &edge;
        continue;
      }

      // Check edge 2
      foundEdge = edge.vertex(0).getID() == vertices[2]->getID();
      foundEdge &= edge.vertex(1).getID() == vertices[3]->getID();
      if ( foundEdge ){
        edges[2] = &edge;
        continue;
      }
      foundEdge = edge.vertex(0).getID() == vertices[3]->getID();
      foundEdge &= edge.vertex(1).getID() == vertices[2]->getID();
      if ( foundEdge ){
        edges[2] = &edge;
        continue;
      }

      // Check edge 3
      foundEdge = edge.vertex(0).getID() == vertices[3]->getID();
      foundEdge &= edge.vertex(1).getID() == vertices[0]->getID();
      if ( foundEdge ){
        edges[3] = &edge;
        continue;
      }
      foundEdge = edge.vertex(0).getID() == vertices[0]->getID();
      foundEdge &= edge.vertex(1).getID() == vertices[3]->getID();
      if ( foundEdge ){
        edges[3] = &edge;
        continue;
      }
    }
    // Create missing edges
    if (edges[0] == nullptr){
      edges[0] = & mesh->createEdge(*vertices[0], *vertices[1]);
    }
    if (edges[1] == nullptr){
      edges[1] = & mesh->createEdge(*vertices[1], *vertices[2]);
    }
    if (edges[2] == nullptr){
      edges[2] = & mesh->createEdge(*vertices[2], *vertices[3]);
    }
    if (edges[3] == nullptr){
      edges[3] = & mesh->createEdge(*vertices[3], *vertices[0]);
    }

    mesh->createQuad(*edges[0], *edges[1], *edges[2], *edges[3]);
  }
}

void SolverInterfaceImpl:: mapWriteDataFrom
(
  int fromMeshID )
{
  preciceTrace("mapWriteDataFrom(int)", fromMeshID);
  if (_clientMode){
    _requestManager->requestMapWriteDataFrom(fromMeshID);
    return;
  }
  impl::MeshContext& context = _accessor->meshContext(fromMeshID);
  impl::MappingContext& mappingContext = context.fromMappingContext;
  if (mappingContext.mapping.use_count() == 0){
    preciceError("mapWriteDataFrom()", "From mesh \"" << context.mesh->getName()
                   << "\", there is no mapping defined");
    return;
  }
  if (not mappingContext.mapping->hasComputedMapping()){
    DEBUG("Compute mapping from mesh \"" << context.mesh->getName() << "\"");
    mappingContext.mapping->computeMapping();
  }
  for (impl::DataContext& context : _accessor->writeDataContexts()) {
    if (context.mesh->getID() == fromMeshID){
      int inDataID = context.fromData->getID();
      int outDataID = context.toData->getID();
      context.toData->values() = Eigen::VectorXd::Zero(context.toData->values().size());
      //assign(context.toData->values()) = 0.0;
      DEBUG("Map data \"" << context.fromData->getName()
                   << "\" from mesh \"" << context.mesh->getName() << "\"");
      assertion(mappingContext.mapping==context.mappingContext.mapping);
      mappingContext.mapping->map(inDataID, outDataID);
    }
  }
  mappingContext.hasMappedData = true;
}


void SolverInterfaceImpl:: mapReadDataTo
(
  int toMeshID )
{
  preciceTrace ("mapReadDataTo(int)", toMeshID);
  if (_clientMode){
    _requestManager->requestMapReadDataTo(toMeshID);
    return;
  }
  impl::MeshContext& context = _accessor->meshContext(toMeshID);
  impl::MappingContext& mappingContext = context.toMappingContext;
  if (mappingContext.mapping.use_count() == 0){
    preciceError("mapReadDataFrom()", "From mesh \"" << context.mesh->getName()
                   << "\", there is no mapping defined!");
    return;
  }
  if (not mappingContext.mapping->hasComputedMapping()){
    DEBUG("Compute mapping from mesh \"" << context.mesh->getName() << "\"");
    mappingContext.mapping->computeMapping();
  }
  for (impl::DataContext& context : _accessor->readDataContexts()) {
    if (context.mesh->getID() == toMeshID){
      int inDataID = context.fromData->getID();
      int outDataID = context.toData->getID();
      context.toData->values() = Eigen::VectorXd::Zero(context.toData->values().size());
      //assign(context.toData->values()) = 0.0;
      DEBUG("Map data \"" << context.fromData->getName()
                   << "\" to mesh \"" << context.mesh->getName() << "\"");
      assertion(mappingContext.mapping==context.mappingContext.mapping);
      mappingContext.mapping->map(inDataID, outDataID);
#     ifdef Debug
      int max = context.toData->values().size();
      std::ostringstream stream;
      for (int i=0; (i < max) && (i < 10); i++){
        stream << context.toData->values()[i] << " ";
      }
      DEBUG("First mapped values = " << stream.str());
#     endif
    }
  }
  mappingContext.hasMappedData = true;
}

void SolverInterfaceImpl:: writeBlockVectorData
(
  int     fromDataID,
  int     size,
  int*    valueIndices,
  double* values )
{
  preciceTrace("writeBlockVectorData()", fromDataID, size);
  if (size == 0)
    return;
  assertion(valueIndices != nullptr);
  assertion(values != nullptr);
  if (_clientMode){
    _requestManager->requestWriteBlockVectorData(fromDataID, size, valueIndices, values);
  }
  else { //couplingMode
    preciceCheck(_accessor->isDataUsed(fromDataID), "writeBlockVectorData()",
                 "You try to write to data that is not defined for " << _accessor->getName());
    DataContext& context = _accessor->dataContext(fromDataID);

    assertion(context.toData.get() != nullptr);
    auto& valuesInternal = context.fromData->values();
    for (int i=0; i < size; i++){
      int offsetInternal = valueIndices[i]*_dimensions;
      int offset = i*_dimensions;
      for (int dim=0; dim < _dimensions; dim++){
        assertion(offset+dim < valuesInternal.size(),
                   offset+dim, valuesInternal.size());
        valuesInternal[offsetInternal + dim] = values[offset + dim];
      }
    }
  }
}

void SolverInterfaceImpl:: writeVectorData
(
  int           fromDataID,
  int           valueIndex,
  const double* value )
{
  TRACE(fromDataID, valueIndex );
# ifdef Debug
  if (_dimensions == 2) DEBUG("value = " << Eigen::Map<const Eigen::Vector2d>(value));
  if (_dimensions == 3) DEBUG("value = " << Eigen::Map<const Eigen::Vector3d>(value));
# endif
  CHECK ( valueIndex >= -1,
          "Invalid value index (" << valueIndex << ") when writing vector data!" );
  if (_clientMode){
    Eigen::VectorXd valueCopy(_dimensions);
    for (int dim=0; dim < _dimensions; dim++){
      valueCopy[dim] = value[dim];
    }
    _requestManager->requestWriteVectorData(fromDataID, valueIndex, valueCopy.data());
  }
  else {
    CHECK(_accessor->isDataUsed(fromDataID),
          "You try to write to data that is not defined for " << _accessor->getName());
    DataContext& context = _accessor->dataContext(fromDataID);
    assertion(context.toData.get() != nullptr);
    auto& values = context.fromData->values();
    assertion(valueIndex >= 0, valueIndex);
    int offset = valueIndex * _dimensions;
    for (int dim=0; dim < _dimensions; dim++){
      values[offset+dim] = value[dim];
    }

  }
}

void SolverInterfaceImpl:: writeBlockScalarData
(
  int     fromDataID,
  int     size,
  int*    valueIndices,
  double* values )
{
  TRACE(fromDataID, size);
  if (size == 0)
    return;
  assertion(valueIndices != nullptr);
  assertion(values != nullptr);
  if (_clientMode){
    _requestManager->requestWriteBlockScalarData(fromDataID, size, valueIndices, values);
  }
  else {
    CHECK(_accessor->isDataUsed(fromDataID),
          "You try to write to data that is not defined for " << _accessor->getName());
    DataContext& context = _accessor->dataContext(fromDataID);
    assertion(context.toData.get() != nullptr);
    auto& valuesInternal = context.fromData->values();
    for (int i=0; i < size; i++){
      assertion(i < valuesInternal.size(), i, valuesInternal.size());
      valuesInternal[valueIndices[i]] = values[i];
    }
  }
}

void SolverInterfaceImpl:: writeScalarData
(
  int    fromDataID,
  int    valueIndex,
  double value )
{
  preciceTrace("writeScalarData()", fromDataID, valueIndex, value );
  preciceCheck(valueIndex >= -1, "writeScalarData()", "Invalid value index ("
               << valueIndex << ") when writing scalar data!");
  if (_clientMode){
    _requestManager->requestWriteScalarData(fromDataID, valueIndex, value);
  }
  else {
    preciceCheck(_accessor->isDataUsed(fromDataID), "writeScalarData()",
                 "You try to write to data that is not defined for " << _accessor->getName());
    DataContext& context = _accessor->dataContext(fromDataID);
    assertion(context.toData.use_count() > 0);
    auto& values = context.fromData->values();
    assertion(valueIndex >= 0, valueIndex);
    values[valueIndex] = value;

  }
}

void SolverInterfaceImpl:: readBlockVectorData
(
  int     toDataID,
  int     size,
  int*    valueIndices,
  double* values )
{
  preciceTrace("readBlockVectorData()", toDataID, size);
  if (size == 0)
    return;
  assertion(valueIndices != nullptr);
  assertion(values != nullptr);
  if (_clientMode){
    _requestManager->requestReadBlockVectorData(toDataID, size, valueIndices, values);
  }
  else { //couplingMode
    preciceCheck(_accessor->isDataUsed(toDataID), "readBlockVectorData()",
                 "You try to read from data that is not defined for " << _accessor->getName());
    DataContext& context = _accessor->dataContext(toDataID);
    assertion(context.fromData.get() != nullptr);
    auto& valuesInternal = context.toData->values();
    for (int i=0; i < size; i++){
      int offsetInternal = valueIndices[i] * _dimensions;
      int offset = i * _dimensions;
      for (int dim=0; dim < _dimensions; dim++){
        assertion(offsetInternal+dim < valuesInternal.size(),
                   offsetInternal+dim, valuesInternal.size());
        values[offset + dim] = valuesInternal[offsetInternal + dim];
      }
    }
  }
}

void SolverInterfaceImpl:: readVectorData
(
  int     toDataID,
  int     valueIndex,
  double* value )
{
  preciceTrace("readVectorData()", toDataID, valueIndex);
  preciceCheck(valueIndex >= -1, "readData(vector)", "Invalid value index ( "
               << valueIndex << " )when reading vector data!");
  if (_clientMode){
    _requestManager->requestReadVectorData(toDataID, valueIndex, value);
  }
  else {
    preciceCheck(_accessor->isDataUsed(toDataID), "readVectorData()",
                     "You try to read from data that is not defined for " << _accessor->getName());
    DataContext& context = _accessor->dataContext(toDataID);
    assertion(context.fromData.use_count() > 0);
    auto& values = context.toData->values();
    assertion (valueIndex >= 0, valueIndex);
    int offset = valueIndex * _dimensions;
    for (int dim=0; dim < _dimensions; dim++){
      value[dim] = values[offset + dim];
    }

  }
# ifdef Debug
  if (_dimensions == 2) DEBUG("value = " << Eigen::Map<Eigen::Vector2d>(value));
  if (_dimensions == 3) DEBUG("value = " << Eigen::Map<Eigen::Vector3d>(value));
# endif
}

void SolverInterfaceImpl:: readBlockScalarData
(
  int     toDataID,
  int     size,
  int*    valueIndices,
  double* values )
{
  preciceTrace("readBlockScalarData()", toDataID, size);
  if (size == 0)
    return;
  DEBUG("size = " << size);
  assertion(valueIndices != nullptr);
  assertion(values != nullptr);
  if (_clientMode){
    _requestManager->requestReadBlockScalarData(toDataID, size, valueIndices, values);
  }
  else {
    preciceCheck(_accessor->isDataUsed(toDataID), "readBlockScalarData()",
                     "You try to read from data that is not defined for " << _accessor->getName());
    DataContext& context = _accessor->dataContext(toDataID);
    assertion(context.fromData.get() != nullptr);
    auto& valuesInternal = context.toData->values();
    for (int i=0; i < size; i++){
      assertion(valueIndices[i] < valuesInternal.size(),
               valueIndices[i], valuesInternal.size());
      values[i] = valuesInternal[valueIndices[i]];
    }
  }
}

void SolverInterfaceImpl:: readScalarData
(
  int     toDataID,
  int     valueIndex,
  double& value )
{
  preciceTrace("readScalarData()", toDataID, valueIndex, value);
  preciceCheck(valueIndex >= -1, "readData(vector)", "Invalid value index ( "
               << valueIndex << " )when reading vector data!");
  if (_clientMode){
    _requestManager->requestReadScalarData(toDataID, valueIndex, value);
  }
  else {
    preciceCheck(_accessor->isDataUsed(toDataID), "readScalarData()",
                     "You try to read from data that is not defined for " << _accessor->getName());
    DataContext& context = _accessor->dataContext(toDataID);
    assertion(context.fromData.use_count() > 0);
    auto& values = context.toData->values();
    value = values[valueIndex];

  }
  DEBUG("Read value = " << value);
}

void SolverInterfaceImpl:: exportMesh
(
  const std::string& filenameSuffix,
  int                exportType )
{
  preciceTrace ( "exportMesh()", filenameSuffix, exportType );
  if ( _clientMode ){
    _requestManager->requestExportMesh ( filenameSuffix, exportType );
    return;
  }
  // Export meshes
  //const ExportContext& context = _accessor->exportContext();
  for (const io::ExportContext& context : _accessor->exportContexts()) {
    DEBUG ( "Export type = " << exportType );
    bool exportAll = exportType == constants::exportAll();
    bool exportThis = context.exporter->getType() == exportType;
    if ( exportAll || exportThis ){
      for (MeshContext* meshContext : _accessor->usedMeshContexts()) {
        std::string name = meshContext->mesh->getName() + "-" + filenameSuffix;
        DEBUG ( "Exporting mesh to file \"" << name << "\" at location \"" << context.location << "\"" );
        context.exporter->doExport ( name, context.location, *(meshContext->mesh) );
      }
    }
    // Export spacetrees
    if (context.exportSpacetree){
      for ( MeshContext* meshContext : _accessor->usedMeshContexts()) {
        std::string name = meshContext->mesh->getName() + "-" + filenameSuffix + ".spacetree";
        if ( meshContext->spacetree.get() != nullptr ) {
          spacetree::ExportSpacetree exportSpacetree(context.location, name);
          exportSpacetree.doExport ( *(meshContext->spacetree) );
        }
      }
    }
  }
  // Export neighbors
  //if ( context.plotNeighbors ) {
  //  _exportVTKNeighbors.exportNeighbors ( filenameSuffix + ".neighbors" );
  //}
}


MeshHandle SolverInterfaceImpl:: getMeshHandle
(
  const std::string& meshName )
{
  preciceTrace("getMeshHandle()", meshName);
  assertion(not _clientMode);
  for (MeshContext* context : _accessor->usedMeshContexts()){
    if (context->mesh->getName() == meshName){
      return MeshHandle(context->mesh->content());
    }
  }
  preciceError("getMeshHandle()", "Participant \"" << _accessorName
               << "\" does not use mesh \"" << meshName << "\"!");
}

void SolverInterfaceImpl:: runServer()
{
  assertion(_serverMode);
  initializeClientServerCommunication();
  _requestManager->handleRequests();
}

void SolverInterfaceImpl:: configureM2Ns
(
  const m2n::M2NConfiguration::SharedPointer& config )
{
  preciceTrace("configureM2Ns()");
  typedef m2n::M2NConfiguration::M2NTuple M2NTuple;
  for (M2NTuple m2nTuple : config->m2ns()) {
    std::string comPartner("");
    bool isRequesting = false;
    if (std::get<1>(m2nTuple) == _accessorName){
      comPartner = std::get<2>(m2nTuple);
      isRequesting = true;
    }
    else if (std::get<2>(m2nTuple) == _accessorName){
      comPartner = std::get<1>(m2nTuple);
    }
    if (not comPartner.empty()){
      for (const impl::PtrParticipant& participant : _participants) {
        if (participant->getName() == comPartner){
          if (participant->useServer()){
            comPartner += "Server";
          }
          assertion(not utils::contained(comPartner, _m2ns), comPartner);
          assertion(std::get<0>(m2nTuple).use_count() > 0);
          M2NWrap m2nWrap;
          m2nWrap.m2n = std::get<0>(m2nTuple);
          m2nWrap.isRequesting = isRequesting;
          _m2ns[comPartner] = m2nWrap;
        }
      }
    }
  }
}

void SolverInterfaceImpl:: configureSolverGeometries
(
  const m2n::M2NConfiguration::SharedPointer& m2nConfig )
{
  preciceTrace ( "configureSolverGeometries()" );
  Eigen::VectorXd offset = Eigen::VectorXd::Zero(_dimensions);
  for (MeshContext* context : _accessor->usedMeshContexts()) {
    if ( context->provideMesh ) { // Accessor provides geometry
      CHECK ( context->receiveMeshFrom.empty(),
              "Participant \"" << _accessorName << "\" cannot provide "
              << "and receive mesh " << context->mesh->getName() << "!" );
      CHECK ( context->geometry.use_count() == 0,
              "Participant \"" << _accessorName << "\" cannot provide "
              << "the geometry of mesh \"" << context->mesh->getName()
              << " in addition to a defined geometry!" );

      bool addedReceiver = false;
      geometry::CommunicatedGeometry* comGeo = nullptr;
      for (PtrParticipant receiver : _participants ) {
        for (MeshContext* receiverContext : receiver->usedMeshContexts()) {
          bool doesReceive = receiverContext->receiveMeshFrom == _accessorName;
          doesReceive &= receiverContext->mesh->getName() == context->mesh->getName();
          if ( doesReceive ){
            DEBUG ( "   ... receiver " << receiver );
            std::string provider ( _accessorName );

            if(!addedReceiver){
              comGeo = new geometry::CommunicatedGeometry ( offset, provider, provider,nullptr);
              context->geometry = geometry::PtrGeometry ( comGeo );
            }
            else{
              DEBUG ( "Further receiver added.");
            }

            // meshRequirement has to be copied from "from" to provide", since
            // mapping are only defined at "provide"
            if(receiverContext->meshRequirement > context->meshRequirement){
              context->meshRequirement = receiverContext->meshRequirement;
            }

            m2n::M2N::SharedPointer m2n =
                m2nConfig->getM2N( receiver->getName(), provider );
            comGeo->addReceiver ( receiver->getName(), m2n );
            m2n->createDistributedCommunication(context->mesh);

            addedReceiver = true;
          }
        }
      }
      if(!addedReceiver){
        DEBUG ( "No receiver found, create SolverGeometry");
        context->geometry = geometry::PtrGeometry ( new geometry::SolverGeometry ( offset) );
      }

      assertion(context->geometry.use_count() > 0);

    }
    else if ( not context->receiveMeshFrom.empty()) { // Accessor receives geometry
      CHECK ( not context->provideMesh, "Participant \"" << _accessorName << "\" cannot provide "
                     << "and receive mesh " << context->mesh->getName() << "!" );
      std::string receiver ( _accessorName );
      std::string provider ( context->receiveMeshFrom );
      DEBUG ( "Receiving mesh from " << provider );
      geometry::impl::PtrDecomposition decomp = nullptr;
      if(context->doesPreFiltering){
        decomp = geometry::impl::PtrDecomposition(
          new geometry::impl::PreFilterPostFilterDecomposition(_dimensions, context->safetyFactor));
      } else {
        decomp = geometry::impl::PtrDecomposition(
          new geometry::impl::BroadcastFilterDecomposition(_dimensions, context->safetyFactor));
      }
      geometry::CommunicatedGeometry * comGeo =
          new geometry::CommunicatedGeometry ( offset, receiver, provider, decomp );
      m2n::M2N::SharedPointer m2n = m2nConfig->getM2N ( receiver, provider );
      comGeo->addReceiver ( receiver, m2n );
      m2n->createDistributedCommunication(context->mesh);
      preciceCheck ( context->geometry.use_count() == 0, "configureSolverGeometries()",
                     "Participant \"" << _accessorName << "\" cannot receive "
                     << "the geometry of mesh \"" << context->mesh->getName()
                     << " in addition to a defined geometry!" );
      if(utils::MasterSlave::_slaveMode || utils::MasterSlave::_masterMode){
        decomp->setBoundingFromMapping(context->fromMappingContext.mapping);
        decomp->setBoundingToMapping(context->toMappingContext.mapping);
      }
      context->geometry = geometry::PtrGeometry ( comGeo );
    }
  }
}

void SolverInterfaceImpl:: prepareGeometry
(
  MeshContext& meshContext )
{
  TRACE(meshContext.mesh->getName());
  assertion ( not _clientMode );
  mesh::PtrMesh mesh = meshContext.mesh;
  assertion(mesh.use_count() > 0);
  std::string meshName(mesh->getName());
  if (_restartMode){
    std::string fileName("precice_checkpoint_" + _accessorName + "_" + meshName);
    DEBUG("Importing geometry = " << mesh->getName());
    geometry::ImportGeometry* importGeo = new geometry::ImportGeometry (
      Eigen::VectorXd::Zero(_dimensions), fileName,
      geometry::ImportGeometry::VRML_1_FILE, true, not meshContext.provideMesh);
    meshContext.geometry.reset(importGeo);
  }
  else if ( (not _geometryMode) && (meshContext.geometry.use_count() > 0) ){
    Eigen::VectorXd offset(meshContext.geometry->getOffset());
    offset += meshContext.localOffset;
    DEBUG("Adding local offset = " << meshContext.localOffset
                 << " to mesh " << mesh->getName());
    meshContext.geometry->setOffset(offset);
  }

  assertion(not (_geometryMode && (meshContext.geometry.use_count() == 0)));
  if (meshContext.geometry.use_count() > 0){
    meshContext.geometry->prepare(*mesh);
  }
}

void SolverInterfaceImpl:: createGeometry
(
  MeshContext& meshContext )
{
  TRACE(meshContext.mesh->getName());
  assertion ( not _clientMode );
  mesh::PtrMesh mesh = meshContext.mesh;
  assertion(mesh.use_count() > 0);
  std::string meshName(mesh->getName());

  assertion(not (_geometryMode && (meshContext.geometry.use_count() == 0)));
  if (meshContext.geometry.use_count() > 0){
    meshContext.geometry->create(*mesh);
    DEBUG("Created geometry \"" << meshName
                 << "\" with # vertices = " << mesh->vertices().size());
    mesh->computeDistribution();
  }

  // Create spacetree for the geometry, if configured so
  if (meshContext.spacetree.use_count() > 0){
    preciceCheck(_geometryMode, "createMeshContext()",
                 "Creating spacetree in coupling mode!");
    meshContext.spacetree->addMesh(mesh);
  }
}

void SolverInterfaceImpl:: mapWrittenData()
{
  preciceTrace("mapWrittenData()");
  using namespace mapping;
  MappingConfiguration::Timing timing;
  // Compute mappings
  for (impl::MappingContext& context : _accessor->writeMappingContexts()) {
    timing = context.timing;
    bool rightTime = timing == MappingConfiguration::ON_ADVANCE;
    rightTime |= timing == MappingConfiguration::INITIAL;
    bool hasComputed = context.mapping->hasComputedMapping();
    if (rightTime && not hasComputed){
      preciceInfo("mapWrittenData()","Compute write mapping from mesh \""
          << _accessor->meshContext(context.fromMeshID).mesh->getName()
          << "\" to mesh \""
          << _accessor->meshContext(context.toMeshID).mesh->getName()
          << "\".");

      context.mapping->computeMapping();
    }
  }

  // Map data
  for (impl::DataContext& context : _accessor->writeDataContexts()) {
    timing = context.mappingContext.timing;
    bool hasMapping = context.mappingContext.mapping.get() != nullptr;
    bool rightTime = timing == MappingConfiguration::ON_ADVANCE;
    rightTime |= timing == MappingConfiguration::INITIAL;
    bool hasMapped = context.mappingContext.hasMappedData;
    if (hasMapping && rightTime && (not hasMapped)){
      int inDataID = context.fromData->getID();
      int outDataID = context.toData->getID();
      DEBUG("Map data \"" << context.fromData->getName()
                   << "\" from mesh \"" << context.mesh->getName() << "\"");
      context.toData->values() = Eigen::VectorXd::Zero(context.toData->values().size());
      //assign(context.toData->values()) = 0.0;
      DEBUG("Map from dataID " << inDataID << " to dataID: " << outDataID);
      context.mappingContext.mapping->map(inDataID, outDataID);
#     ifdef Debug
      int max = context.toData->values().size();
      std::ostringstream stream;
      for (int i=0; (i < max) && (i < 10); i++){
        stream << context.toData->values()[i] << " ";
      }
      DEBUG("First mapped values = " << stream.str() );
#     endif
    }
  }

  // Clear non-stationary, non-incremental mappings
  for (impl::MappingContext& context : _accessor->writeMappingContexts()) {
    bool isStationary = context.timing
                        == MappingConfiguration::INITIAL;
    if (not isStationary){
        context.mapping->clear();
    }
    context.hasMappedData = false;
  }
}

void SolverInterfaceImpl:: mapReadData()
{
  preciceTrace("mapReadData()");
  mapping::MappingConfiguration::Timing timing;
  // Compute mappings
  for (impl::MappingContext& context : _accessor->readMappingContexts()) {
    timing = context.timing;
    bool mapNow = timing == mapping::MappingConfiguration::ON_ADVANCE;
    mapNow |= timing == mapping::MappingConfiguration::INITIAL;
    bool hasComputed = context.mapping->hasComputedMapping();
    if (mapNow && not hasComputed){
      preciceInfo("mapReadData()","Compute read mapping from mesh \""
              << _accessor->meshContext(context.fromMeshID).mesh->getName()
              << "\" to mesh \""
              << _accessor->meshContext(context.toMeshID).mesh->getName()
              << "\".");

      context.mapping->computeMapping();
    }
  }

  // Map data
  for (impl::DataContext& context : _accessor->readDataContexts()) {
    timing = context.mappingContext.timing;
    bool mapNow = timing == mapping::MappingConfiguration::ON_ADVANCE;
    mapNow |= timing == mapping::MappingConfiguration::INITIAL;
    bool hasMapping = context.mappingContext.mapping.get() != nullptr;
    bool hasMapped = context.mappingContext.hasMappedData;
    if (mapNow && hasMapping && (not hasMapped)){
      int inDataID = context.fromData->getID();
      int outDataID = context.toData->getID();
      context.toData->values() = Eigen::VectorXd::Zero(context.toData->values().size());
      //assign(context.toData->values()) = 0.0;
      DEBUG("Map read data \"" << context.fromData->getName()
                   << "\" to mesh \"" << context.mesh->getName() << "\"");
      context.mappingContext.mapping->map(inDataID, outDataID);
#     ifdef Debug
      int max = context.toData->values().size();
      std::ostringstream stream;
      for (int i=0; (i < max) && (i < 10); i++){
        stream << context.toData->values()[i] << " ";
      }
      DEBUG("First mapped values = " << stream.str());
#     endif
    }
  }

  // Clear non-initial, non-incremental mappings
  for (impl::MappingContext& context : _accessor->readMappingContexts()) {
    bool isStationary = context.timing
              == mapping::MappingConfiguration::INITIAL;
    if (not isStationary){
      context.mapping->clear();
    }
    context.hasMappedData = false;
  }
}

void SolverInterfaceImpl:: performDataActions
(
  const std::set<action::Action::Timing>& timings,
  double                 time,
  double                 dt,
  double                 partFullDt,
  double                 fullDt )
{
  preciceTrace("performDataActions()");
  assertion(not _clientMode);
  for (action::PtrAction& action : _accessor->actions()) {
    if (timings.find(action->getTiming()) != timings.end()){
      action->performAction(time, dt, partFullDt, fullDt);
    }
  }
}

void SolverInterfaceImpl:: handleExports()
{
  preciceTrace("handleExports()");
  assertion(not _clientMode);
  //timesteps was already incremented before
  int timesteps = _couplingScheme->getTimesteps()-1;

  for (const io::ExportContext& context : _accessor->exportContexts()) {
    if (_couplingScheme->isCouplingTimestepComplete() || context.everyIteration){
      if (context.timestepInterval != -1){
        if (timesteps % context.timestepInterval == 0){
          if (context.everyIteration){
            std::ostringstream everySuffix;
            everySuffix << _accessorName << ".it" << _numberAdvanceCalls;
            exportMesh(everySuffix.str());
          }
          std::ostringstream suffix;
          suffix << _accessorName << ".dt" << _couplingScheme->getTimesteps()-1;
          exportMesh(suffix.str());
          if (context.triggerSolverPlot){
            _couplingScheme->requireAction(constants::actionPlotOutput());
          }
        }
      }
    }
  }

  if (_couplingScheme->isCouplingTimestepComplete()){
    // Export watch point data
    for (PtrWatchPoint watchPoint : _accessor->watchPoints()) {
      watchPoint->exportPointData(_couplingScheme->getTime());
    }

    if(not utils::MasterSlave::_slaveMode){ //TODO not yet supported
      // Checkpointing
      int checkpointingInterval = _couplingScheme->getCheckpointTimestepInterval();
      if ((checkpointingInterval != -1) && (timesteps % checkpointingInterval == 0)){
        DEBUG("Set require checkpoint");
        _couplingScheme->requireAction(constants::actionWriteSimulationCheckpoint());
        for (const MeshContext* meshContext : _accessor->usedMeshContexts()) {
          io::ExportVRML exportVRML(false);
          std::string filename("precice_checkpoint_" + _accessorName
                               + "_" + meshContext->mesh->getName());
          exportVRML.doExportCheckpoint(filename, *(meshContext->mesh));
        }
        io::SimulationStateIO exportState(_checkpointFileName + "_simstate.txt");

        exportState.writeState(_couplingScheme->getTime(),_couplingScheme->getTimesteps(), _numberAdvanceCalls);
        //io::TXTWriter exportCouplingSchemeState(_checkpointFileName + "_cplscheme.txt");
        _couplingScheme->exportState(_checkpointFileName);
      }
    }
  }
}

void SolverInterfaceImpl:: resetWrittenData()
{
  preciceTrace("resetWrittenData()");
  for (DataContext& context : _accessor->writeDataContexts()) {
    context.fromData->values() = Eigen::VectorXd::Zero(context.fromData->values().size());
    //assign(context.fromData->values()) = 0.0;
    if (context.toData != context.fromData){
      context.toData->values() = Eigen::VectorXd::Zero(context.toData->values().size());
      //assign(context.toData->values()) = 0.0;
    }
  }
//  if ( _accessor->exportContext().plotNeighbors ){
//    _exportVTKNeighbors.resetElements ();
//  }
}

//void SolverInterfaceImpl:: resetDataIndices()
//{
//  preciceTrace ( "resetDataIndices()" );
//  for ( DataContext & context : _accessor->writeDataContexts() ){
//    context.indexCursor = 0;
//  }
//  for ( DataContext & context : _accessor->readDataContexts() ){
//    context.indexCursor = 0;
//  }
//}

PtrParticipant SolverInterfaceImpl:: determineAccessingParticipant
(
   const config::SolverInterfaceConfiguration& config )
{
  config::PtrParticipantConfiguration partConfig =
      config.getParticipantConfiguration ();
  for (const PtrParticipant& participant : partConfig->getParticipants()) {
    if ( participant->getName() == _accessorName ) {
      return participant;
    }
  }
  preciceError ( "determineAccessingParticipant()",
                 "Accessing participant \"" << _accessorName << "\" is not defined"
                 << " in configuration!" );
}

void SolverInterfaceImpl:: selectInquiryMeshIDs
(
  const std::set<int>& meshIDs,
  std::vector<int>&    markedMeshContexts ) const
{
  preciceTrace("selectInquiryMeshIDs()", meshIDs.size());
  assertion(markedMeshContexts.size() == _accessor->usedMeshContexts().size(),
             markedMeshContexts.size(), _accessor->usedMeshContexts().size());

  if (meshIDs.empty()){ // All mesh IDs are used in inquiry
    for (int i=0; i < (int)markedMeshContexts.size(); i++){
      const MeshContext* context = _accessor->usedMeshContexts()[i];
      if (context->spacetree.get() == nullptr){
        markedMeshContexts[i] = markedQueryDirectly();
      }
      else if (context->mesh->getID() == context->spacetree->meshes().front()->getID()){
        markedMeshContexts[i] = markedQuerySpacetree();
      }
      else {
        markedMeshContexts[i] = markedSkip();
      }
    }
  }
  else {
    for (int i=0; i < (int)markedMeshContexts.size(); i++){
      const MeshContext* context = _accessor->usedMeshContexts()[i];
      if (utils::contained(context->mesh->getID(), meshIDs)){
        if (context->spacetree.get() == nullptr){
          markedMeshContexts[i] = markedQueryDirectly();
        }
        else {
          bool allSpacetreeMeshesAreInquired = true;
          for (const mesh::PtrMesh& mesh : context->spacetree->meshes()) {
            if (not utils::contained(mesh->getID(), meshIDs)){
              allSpacetreeMeshesAreInquired = false;
              break;
            }
          }
          if (allSpacetreeMeshesAreInquired){
            bool isFirst = context->mesh->getID()
                           == context->spacetree->meshes().front()->getID();
            if (isFirst){
              markedMeshContexts[i] = markedQuerySpacetree();
            }
            else {
              // Not selected, since already covered by query of first spacetree
              // mesh.
              markedMeshContexts[i] = markedSkip();
            }
          }
          else {
            markedMeshContexts[i] = markedQueryDirectly();
          }
        }
      }
      else {
        markedMeshContexts[i] = markedSkip();
      }
    }
  }
}

void SolverInterfaceImpl:: initializeClientServerCommunication()
{
  preciceTrace ( "initializeClientServerCom.()" );
  com::Communication::SharedPointer com = _accessor->getClientServerCommunication();
  assertion(com.get() != nullptr);
  if ( _serverMode ){
    preciceInfo ( "initializeClientServerCom.()", "Setting up communication to client" );
    com->acceptConnection ( _accessorName + "Server", _accessorName,
                            _accessorProcessRank, _accessorCommunicatorSize );
  }
  else {
    preciceInfo ( "initializeClientServerCom.()", "Setting up communication to server" );
    com->requestConnection( _accessorName + "Server", _accessorName,
                            _accessorProcessRank, _accessorCommunicatorSize );
  }
}

void SolverInterfaceImpl:: initializeMasterSlaveCommunication()
{
  TRACE();
  //slaves create new communicator with ranks 0 to size-2
  //therefore, the master uses a rankOffset and the slaves have to call request
  // with that offset
  int rankOffset = 1;
  if ( utils::MasterSlave::_masterMode ){
    preciceInfo ( "initializeMasterSlaveCom.()", "Setting up communication to slaves" );
    utils::MasterSlave::_communication->acceptConnection ( _accessorName + "Master", _accessorName,
                            _accessorProcessRank, 1);
    utils::MasterSlave::_communication->setRankOffset(rankOffset);
  }
  else {
    assertion(utils::MasterSlave::_slaveMode);
    utils::MasterSlave::_communication->requestConnection( _accessorName + "Master", _accessorName,
                            _accessorProcessRank-rankOffset, _accessorCommunicatorSize-rankOffset );
  }
}

void SolverInterfaceImpl:: syncTimestep(double computedTimestepLength)
{
  assertion(utils::MasterSlave::_masterMode || utils::MasterSlave::_slaveMode);
  if(utils::MasterSlave::_slaveMode){
    utils::MasterSlave::_communication->send(computedTimestepLength, 0);
  }
  else if(utils::MasterSlave::_masterMode){
    for(int rankSlave = 1; rankSlave < _accessorCommunicatorSize; rankSlave++){
      double dt;
      utils::MasterSlave::_communication->receive(dt, rankSlave);
      CHECK(math::equals(dt, computedTimestepLength),
            "Ambiguous timestep length when calling request advance from several processes!");
    }
  }
}


}} // namespace precice, impl

