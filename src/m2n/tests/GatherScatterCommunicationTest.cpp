#ifndef PRECICE_NO_MPI

#include "GatherScatterCommunicationTest.hpp"
#include "utils/Parallel.hpp"
#include "com/MPIDirectCommunication.hpp"
#include "m2n/M2N.hpp"
#include "m2n/DistributedComFactory.hpp"
#include "m2n/GatherScatterComFactory.hpp"
#include "utils/MasterSlave.hpp"
#include "mesh/Mesh.hpp"
#include "mesh/SharedPointer.hpp"
#include "utils/Helpers.hpp"

#include "tarch/tests/TestCaseFactory.h"
registerTest(precice::m2n::tests::GatherScatterCommunicationTest)

namespace precice {
namespace m2n {
namespace tests {

logging::Logger GatherScatterCommunicationTest::
  _log ( "precice::m2n::tests::GatherScatterCommunicationTest" );

GatherScatterCommunicationTest:: GatherScatterCommunicationTest ()
:
  TestCase ( "m2n::tests::GatherScatterCommunicationTest" )
{}

void GatherScatterCommunicationTest:: run ()
{
  preciceTrace ( "run" );
  typedef utils::Parallel Par;
  if (Par::getCommunicatorSize() > 3){
    MPI_Comm comm = Par::getRestrictedCommunicator({0, 1, 2, 3});
    if (Par::getProcessRank() <= 3){
      Par::setGlobalCommunicator(comm);
      testMethod ( testSendReceiveAll );
      Par::setGlobalCommunicator(Par::getCommunicatorWorld());
    }
  }
}

void GatherScatterCommunicationTest:: testSendReceiveAll ()
{
  preciceTrace ( "testSendReceiveAll" );
  assertion ( utils::Parallel::getCommunicatorSize() == 4 );

  com::Communication::SharedPointer participantCom =
      com::Communication::SharedPointer(new com::MPIDirectCommunication());
  m2n::DistributedComFactory::SharedPointer distrFactory = m2n::DistributedComFactory::SharedPointer(
      new m2n::GatherScatterComFactory(participantCom));
  m2n::M2N::SharedPointer m2n = m2n::M2N::SharedPointer(new m2n::M2N(participantCom, distrFactory));
  com::Communication::SharedPointer masterSlaveCom =
      com::Communication::SharedPointer(new com::MPIDirectCommunication());
  utils::MasterSlave::_communication = masterSlaveCom;

  utils::Parallel::synchronizeProcesses();

  if (utils::Parallel::getProcessRank() == 0){ //Participant 1
    utils::Parallel::splitCommunicator( "Part1" );
    utils::MasterSlave::_rank = 0;
    utils::MasterSlave::_size = 1;
    utils::MasterSlave::_slaveMode = false;
    utils::MasterSlave::_masterMode = false;
  }
  else if(utils::Parallel::getProcessRank() == 1){//Participant 2 - Master
    utils::Parallel::splitCommunicator( "Part2Master" );
    utils::MasterSlave::_rank = 0;
    utils::MasterSlave::_size = 3;
    utils::MasterSlave::_slaveMode = false;
    utils::MasterSlave::_masterMode = true;
    masterSlaveCom->acceptConnection ( "Part2Master", "Part2Slaves", 0, 1);
    masterSlaveCom->setRankOffset(1);
  }
  else if(utils::Parallel::getProcessRank() == 2){//Participant 2 - Slave1
    utils::Parallel::splitCommunicator( "Part2Slaves");
    utils::MasterSlave::_rank = 1;
    utils::MasterSlave::_size = 3;
    utils::MasterSlave::_slaveMode = true;
    utils::MasterSlave::_masterMode = false;
    masterSlaveCom->requestConnection( "Part2Master", "Part2Slaves", 0, 2 );
  }
  else if(utils::Parallel::getProcessRank() == 3){//Participant 2 - Slave2
    utils::Parallel::splitCommunicator( "Part2Slaves");
    utils::MasterSlave::_rank = 2;
    utils::MasterSlave::_size = 3;
    utils::MasterSlave::_slaveMode = true;
    utils::MasterSlave::_masterMode = false;
    masterSlaveCom->requestConnection( "Part2Master", "Part2Slaves", 1, 2 );
  }

  DEBUG("Initialized and MasterSlave connection set up");
  utils::Parallel::synchronizeProcesses();

  if(utils::Parallel::getProcessRank() == 0){//Part1
    m2n->acceptMasterConnection ( "Part1", "Part2Master");
  }
  else if(utils::Parallel::getProcessRank() == 1){//Part2 Master
    m2n->requestMasterConnection ( "Part1", "Part2Master");
  }
  else if(utils::Parallel::getProcessRank() == 2){//Part2 Slave1
    m2n->requestMasterConnection ( "Part1", "Part2Master");
  }
  else if(utils::Parallel::getProcessRank() == 3){//Part2 Slave2
    m2n->requestMasterConnection ( "Part1", "Part2Master");
  }

  DEBUG("MasterMaster connection set up");
  utils::Parallel::synchronizeProcesses();

  int dimensions = 2;
  int numberOfVertices = 6;
  bool flipNormals = false;
  int valueDimension = 1;
  Eigen::VectorXd offset = Eigen::VectorXd::Zero(dimensions);
  
  if (utils::Parallel::getProcessRank() == 0){ //Part1
    mesh::PtrMesh pMesh(new mesh::Mesh("Mesh", dimensions, flipNormals));
    m2n->createDistributedCommunication(pMesh);
    m2n->acceptSlavesConnection ( "Part1", "Part2Master");
    Eigen::VectorXd values = Eigen::VectorXd::Zero(numberOfVertices);
    values << 1.0, 2.0, 3.0, 4.0, 5.0, 6.0;
    m2n->send(values.data(),numberOfVertices,pMesh->getID(),valueDimension);
    m2n->receive(values.data(),numberOfVertices,pMesh->getID(),valueDimension);
    validate(values[0]==2.0);
    validate(values[1]==4.0);
    validate(values[2]==6.0);
    validate(values[3]==16.0);
    validate(values[4]==10.0);
    validate(values[5]==12.0);

  }
  else{
    mesh::PtrMesh pMesh(new mesh::Mesh("Mesh", dimensions, flipNormals));
    m2n->createDistributedCommunication(pMesh);
    m2n->requestSlavesConnection ( "Part1", "Part2Master");

    if(utils::Parallel::getProcessRank() == 1){//Master
      pMesh->setGlobalNumberOfVertices(numberOfVertices);
      pMesh->getVertexDistribution()[0].push_back(0);
      pMesh->getVertexDistribution()[0].push_back(1);
      pMesh->getVertexDistribution()[0].push_back(3);
      pMesh->getVertexDistribution()[2].push_back(2);
      pMesh->getVertexDistribution()[2].push_back(3);
      pMesh->getVertexDistribution()[2].push_back(4);
      pMesh->getVertexDistribution()[2].push_back(5);

      Eigen::Vector3d values(0.0, 0.0, 0.0);
      m2n->receive(values.data(),3,pMesh->getID(),valueDimension);
      validate(values[0]==1.0);
      validate(values[1]==2.0);
      validate(values[2]==4.0);
      values = values * 2;
      m2n->send(values.data(),3,pMesh->getID(),valueDimension);
    }
    else if(utils::Parallel::getProcessRank() == 2){//Slave1
      Eigen::VectorXd values;
      m2n->receive(values.data(),0,pMesh->getID(),valueDimension);
      m2n->send(values.data(),0,pMesh->getID(),valueDimension);
    }
    else if(utils::Parallel::getProcessRank() == 3){//Slave2
      Eigen::Vector4d values(0.0, 0.0, 0.0, 0.0);
      m2n->receive(values.data(),4,pMesh->getID(),valueDimension);
      validate(values[0]==3.0);
      validate(values[1]==4.0);
      validate(values[2]==5.0);
      validate(values[3]==6.0);
      values = values * 2;
      m2n->send(values.data(),4,pMesh->getID(),valueDimension);
    }
  }

  utils::MasterSlave::_communication.reset();
  utils::MasterSlave::_rank = utils::Parallel::getProcessRank();
  utils::MasterSlave::_size = utils::Parallel::getCommunicatorSize();
  utils::MasterSlave::_slaveMode = false;
  utils::MasterSlave::_masterMode = false;

  utils::Parallel::synchronizeProcesses();
  utils::Parallel::clearGroups();
}

}}} // namespace precice, m2n, tests

#endif // PRECICE_NO_MPI
