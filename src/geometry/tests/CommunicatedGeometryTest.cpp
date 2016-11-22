#ifndef PRECICE_NO_MPI

#include "CommunicatedGeometryTest.hpp"
#include "geometry/Geometry.hpp"
#include "geometry/SharedPointer.hpp"
#include "geometry/CommunicatedGeometry.hpp"
#include "geometry/impl/Decomposition.hpp"
#include "geometry/impl/PreFilterPostFilterDecomposition.hpp"
#include "geometry/impl/BroadcastFilterDecomposition.hpp"
#include "mesh/Mesh.hpp"
#include "utils/Parallel.hpp"
#include "mapping/SharedPointer.hpp"
#include "mapping/NearestProjectionMapping.hpp"
#include "mapping/NearestNeighborMapping.hpp"
#include "com/MPIDirectCommunication.hpp"
#include "m2n/M2N.hpp"
#include "m2n/GatherScatterComFactory.hpp"
#include "utils/Globals.hpp"
#include "utils/MasterSlave.hpp"

#include "tarch/tests/TestCaseFactory.h"
registerTest(precice::geometry::tests::CommunicatedGeometryTest)

namespace precice {
namespace geometry {
namespace tests {

logging::Logger CommunicatedGeometryTest::
  _log ( "precice::geometry::tests::CommunicatedGeometryTest" );

CommunicatedGeometryTest:: CommunicatedGeometryTest ()
:
  TestCase ( "geometry::tests::CommunicatedGeometryTest" )
{}

void CommunicatedGeometryTest:: run ()
{
  preciceTrace ( "run" );
  typedef utils::Parallel Par;
  if (Par::getCommunicatorSize() > 3){
    const std::vector<int> ranksWanted = {0, 1, 2, 3};
    MPI_Comm comm = Par::getRestrictedCommunicator(ranksWanted);
    if (Par::getProcessRank() <= 3){
      Par::setGlobalCommunicator(comm);
      testMethod ( testScatterMesh );
      Par::setGlobalCommunicator(Par::getCommunicatorWorld());
    }
    comm = Par::getRestrictedCommunicator(ranksWanted);
    if (Par::getProcessRank() <= 3){
      Par::setGlobalCommunicator(comm); //necessary to be able to re-initialize with different leading ranks
      testMethod ( testGatherMesh );
      Par::setGlobalCommunicator(Par::getCommunicatorWorld());
    }
  }
}

void CommunicatedGeometryTest:: testScatterMesh ()
{
  preciceTrace ( "testScatterMesh" );
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

  if (utils::Parallel::getProcessRank() == 0){ //SOLIDZ
    utils::Parallel::splitCommunicator( "SOLIDZ" );
    m2n->acceptMasterConnection ( "SOLIDZ", "NASTINMaster");
  }
  else if(utils::Parallel::getProcessRank() == 1){//Master
    utils::Parallel::splitCommunicator( "NASTINMaster" );
    m2n->requestMasterConnection ( "SOLIDZ", "NASTINMaster");
  }
  else if(utils::Parallel::getProcessRank() == 2){//Slave1
    utils::Parallel::splitCommunicator( "NASTINSlaves");
  }
  else if(utils::Parallel::getProcessRank() == 3){//Slave2
    utils::Parallel::splitCommunicator( "NASTINSlaves");
  }

  if(utils::Parallel::getProcessRank() == 1){//Master
    masterSlaveCom->acceptConnection ( "NASTINMaster", "NASTINSlaves", 0, 1);
    masterSlaveCom->setRankOffset(1);
  }
  else if(utils::Parallel::getProcessRank() == 2){//Slave1
    masterSlaveCom->requestConnection( "NASTINMaster", "NASTINSlaves", 0, 2 );
  }
  else if(utils::Parallel::getProcessRank() == 3){//Slave2
    masterSlaveCom->requestConnection( "NASTINMaster", "NASTINSlaves", 1, 2 );
  }


  int dimensions = 2;
  bool flipNormals = false;
  Eigen::VectorXd offset = Eigen::VectorXd::Zero(dimensions);
  
  if (utils::Parallel::getProcessRank() == 0){ //SOLIDZ
    utils::MasterSlave::_slaveMode = false;
    utils::MasterSlave::_masterMode = false;
    mesh::PtrMesh pSolidzMesh1(new mesh::Mesh("SolidzMesh1", dimensions, flipNormals));
    mesh::PtrMesh pSolidzMesh2(new mesh::Mesh("SolidzMesh2", dimensions, flipNormals));
    CommunicatedGeometry geo1( offset, "SOLIDZ", "SOLIDZ", nullptr);
    CommunicatedGeometry geo2( offset, "SOLIDZ", "SOLIDZ", nullptr);
    geo1.addReceiver("NASTINMaster",m2n);
    geo2.addReceiver("NASTINMaster",m2n);

    Eigen::VectorXd position(dimensions);
  
    position << 0.0, 0.0;
    mesh::Vertex& v1_1 = pSolidzMesh1->createVertex(position);
    mesh::Vertex& v1_2 = pSolidzMesh2->createVertex(position);
    position << 0.0, 1.95;
    mesh::Vertex& v2_1 = pSolidzMesh1->createVertex(position);
    mesh::Vertex& v2_2 = pSolidzMesh2->createVertex(position);
    position << 0.0, 2.1;
    mesh::Vertex& v3_1 = pSolidzMesh1->createVertex(position);
    mesh::Vertex& v3_2 = pSolidzMesh2->createVertex(position);
    position << 0.0, 4.5;
    mesh::Vertex& v4_1 = pSolidzMesh1->createVertex(position);
    mesh::Vertex& v4_2 = pSolidzMesh2->createVertex(position);
    position << 0.0, 5.95;
    mesh::Vertex& v5_1 = pSolidzMesh1->createVertex(position);
    mesh::Vertex& v5_2 = pSolidzMesh2->createVertex(position);
    position << 0.0, 6.1;
    mesh::Vertex& v6_1 = pSolidzMesh1->createVertex(position);
    mesh::Vertex& v6_2 = pSolidzMesh2->createVertex(position);
    pSolidzMesh1->createEdge(v1_1,v2_1);
    pSolidzMesh1->createEdge(v2_1,v3_1);
    pSolidzMesh1->createEdge(v3_1,v4_1);
    pSolidzMesh1->createEdge(v4_1,v5_1);
    pSolidzMesh1->createEdge(v5_1,v6_1);
    pSolidzMesh2->createEdge(v1_2,v2_2);
    pSolidzMesh2->createEdge(v2_2,v3_2);
    pSolidzMesh2->createEdge(v3_2,v4_2);
    pSolidzMesh2->createEdge(v4_2,v5_2);
    pSolidzMesh2->createEdge(v5_2,v6_2);

    geo1.prepare(*pSolidzMesh1);
    geo2.prepare(*pSolidzMesh2);
    geo1.create(*pSolidzMesh1);
    geo2.create(*pSolidzMesh2);
  }
  else{
    mesh::PtrMesh pNastinMesh(new mesh::Mesh("NastinMesh", dimensions, flipNormals));
    mesh::PtrMesh pSolidzMesh1(new mesh::Mesh("SolidzMesh1", dimensions, flipNormals));
    mesh::PtrMesh pSolidzMesh2(new mesh::Mesh("SolidzMesh2", dimensions, flipNormals));

    mapping::PtrMapping boundingFromMapping1 = mapping::PtrMapping (
        new mapping::NearestNeighborMapping(mapping::Mapping::CONSISTENT, dimensions) );
    mapping::PtrMapping boundingToMapping1 = mapping::PtrMapping (
        new mapping::NearestNeighborMapping(mapping::Mapping::CONSERVATIVE, dimensions) );
    mapping::PtrMapping boundingFromMapping2 = mapping::PtrMapping (
        new mapping::NearestProjectionMapping(mapping::Mapping::CONSISTENT, dimensions) );
    mapping::PtrMapping boundingToMapping2 = mapping::PtrMapping (
        new mapping::NearestNeighborMapping(mapping::Mapping::CONSERVATIVE, dimensions) );
    boundingFromMapping1->setMeshes(pSolidzMesh1,pNastinMesh);
    boundingToMapping1->setMeshes(pNastinMesh,pSolidzMesh1);
    boundingFromMapping2->setMeshes(pSolidzMesh2,pNastinMesh);
    boundingToMapping2->setMeshes(pNastinMesh,pSolidzMesh2);

    if(utils::Parallel::getProcessRank() == 1){//Master
      utils::MasterSlave::_rank = 0;
      utils::MasterSlave::_size = 3;
      utils::MasterSlave::_slaveMode = false;
      utils::MasterSlave::_masterMode = true;
      Eigen::VectorXd position(dimensions);
      position << 0.0, 0.0;
      pNastinMesh->createVertex(position);
      position << 0.0, 2.0;
      pNastinMesh->createVertex(position);
    }
    else if(utils::Parallel::getProcessRank() == 2){//Slave1
      utils::MasterSlave::_rank = 1;
      utils::MasterSlave::_size = 3;
      utils::MasterSlave::_slaveMode = true;
      utils::MasterSlave::_masterMode = false;
    }
    else if(utils::Parallel::getProcessRank() == 3){//Slave2
      utils::MasterSlave::_rank = 2;
      utils::MasterSlave::_size = 3;
      utils::MasterSlave::_slaveMode = true;
      utils::MasterSlave::_masterMode = false;
      Eigen::VectorXd position(dimensions);
      position << 0.0, 4.0;
      pNastinMesh->createVertex(position);
      position << 0.0, 6.0;
      pNastinMesh->createVertex(position);
    }

    pNastinMesh->computeState();

    impl::PtrDecomposition decomp1 = impl::PtrDecomposition(
            new impl::BroadcastFilterDecomposition(dimensions,  0.1));
    impl::PtrDecomposition decomp2 = impl::PtrDecomposition(
            new impl::PreFilterPostFilterDecomposition(dimensions, 0.1));

    CommunicatedGeometry geo1( offset, "NASTINMaster", "SOLIDZ", decomp1);
    CommunicatedGeometry geo2( offset, "NASTINMaster", "SOLIDZ", decomp2);
    decomp1->setBoundingFromMapping(boundingFromMapping1);
    decomp1->setBoundingToMapping(boundingToMapping1);
    decomp2->setBoundingFromMapping(boundingFromMapping2);
    decomp2->setBoundingToMapping(boundingToMapping2);
    geo1.addReceiver("NASTINMaster", m2n);
    geo2.addReceiver("NASTINMaster", m2n);
    geo1.prepare(*pSolidzMesh1);
    geo2.prepare(*pSolidzMesh2);
    geo1.create(*pSolidzMesh1);
    geo2.create(*pSolidzMesh2);


    // check if the sending and filtering worked right
    if(utils::Parallel::getProcessRank() == 1){//Master
      validate(pSolidzMesh1->vertices().size()==2);
      validate(pSolidzMesh1->edges().size()==1);
      validate(pSolidzMesh2->vertices().size()==3);
      validate(pSolidzMesh2->edges().size()==2);
    }
    else if(utils::Parallel::getProcessRank() == 2){//Slave1
      validate(pSolidzMesh1->vertices().size()==0);
      validate(pSolidzMesh1->edges().size()==0);
      validate(pSolidzMesh2->vertices().size()==0);
      validate(pSolidzMesh2->edges().size()==0);
    }
    else if(utils::Parallel::getProcessRank() == 3){//Slave2
      validate(pSolidzMesh1->vertices().size()==2);
      validate(pSolidzMesh1->edges().size()==1);
      validate(pSolidzMesh2->vertices().size()==3);
      validate(pSolidzMesh2->edges().size()==2);
    }

  }
  utils::MasterSlave::_slaveMode = false;
  utils::MasterSlave::_masterMode = false;
  utils::Parallel::clearGroups();
  utils::Parallel::synchronizeProcesses();
}

void CommunicatedGeometryTest:: testGatherMesh ()
{
  preciceTrace ( "testGatherMesh" );
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

  if (utils::Parallel::getProcessRank() == 0){ //NASTIN
    utils::Parallel::splitCommunicator( "NASTIN" );
    m2n->acceptMasterConnection ( "NASTIN", "SOLIDZMaster");
  }
  else if(utils::Parallel::getProcessRank() == 1){//Master
    utils::Parallel::splitCommunicator( "SOLIDZMaster" );
    m2n->requestMasterConnection ( "NASTIN", "SOLIDZMaster" );
  }
  else if(utils::Parallel::getProcessRank() == 2){//Slave1
    utils::Parallel::splitCommunicator( "SOLIDZSlaves");
  }
  else if(utils::Parallel::getProcessRank() == 3){//Slave2
    utils::Parallel::splitCommunicator( "SOLIDZSlaves");
  }

  if(utils::Parallel::getProcessRank() == 1){//Master
    masterSlaveCom->acceptConnection ( "SOLIDZMaster", "SOLIDZSlaves", 0, 1);
    masterSlaveCom->setRankOffset(1);
  }
  else if(utils::Parallel::getProcessRank() == 2){//Slave1
    masterSlaveCom->requestConnection( "SOLIDZMaster", "SOLIDZSlaves", 0, 2 );
  }
  else if(utils::Parallel::getProcessRank() == 3){//Slave2
    masterSlaveCom->requestConnection( "SOLIDZMaster", "SOLIDZSlaves", 1, 2 );
  }


  int dimensions = 2;
  bool flipNormals = false;
  Eigen::VectorXd offset = Eigen::VectorXd::Zero(dimensions);
  
  if (utils::Parallel::getProcessRank() == 0){ //NASTIN
    utils::MasterSlave::_slaveMode = false;
    utils::MasterSlave::_masterMode = false;
    mesh::PtrMesh pSolidzMesh(new mesh::Mesh("SolidzMesh", dimensions, flipNormals));
    CommunicatedGeometry geo( offset, "NASTIN", "SOLIDZMaster", nullptr);
    geo.addReceiver("NASTIN",m2n);
    geo.prepare(*pSolidzMesh);
    geo.create(*pSolidzMesh);
    validate(pSolidzMesh->vertices().size()==6);
    validate(pSolidzMesh->edges().size()==4);

  }
  else{//SOLIDZ
    mesh::PtrMesh pSolidzMesh(new mesh::Mesh("SolidzMesh", dimensions, flipNormals));

    if(utils::Parallel::getProcessRank() == 1){//Master
      utils::MasterSlave::_rank = 0;
      utils::MasterSlave::_size = 3;
      utils::MasterSlave::_slaveMode = false;
      utils::MasterSlave::_masterMode = true;
      Eigen::VectorXd position(dimensions);
      position << 0.0, 0.0;
      mesh::Vertex& v1 = pSolidzMesh->createVertex(position);
      position << 0.0, 1.5;
      mesh::Vertex& v2 = pSolidzMesh->createVertex(position);
      pSolidzMesh->createEdge(v1,v2);
    }
    else if(utils::Parallel::getProcessRank() == 2){//Slave1
      utils::MasterSlave::_rank = 1;
      utils::MasterSlave::_size = 3;
      utils::MasterSlave::_slaveMode = true;
      utils::MasterSlave::_masterMode = false;
    }
    else if(utils::Parallel::getProcessRank() == 3){//Slave2
      utils::MasterSlave::_rank = 2;
      utils::MasterSlave::_size = 3;
      utils::MasterSlave::_slaveMode = true;
      utils::MasterSlave::_masterMode = false;
      Eigen::VectorXd position(dimensions);
      position << 0.0, 3.5;
      mesh::Vertex& v3 = pSolidzMesh->createVertex(position);
      position << 0.0, 4.5;
      mesh::Vertex& v4 = pSolidzMesh->createVertex(position);
      position << 0.0, 5.5;
      mesh::Vertex& v5 = pSolidzMesh->createVertex(position);
      position << 0.0, 7.0;
      mesh::Vertex& v6 = pSolidzMesh->createVertex(position);
      pSolidzMesh->createEdge(v3,v4);
      pSolidzMesh->createEdge(v4,v5);
      pSolidzMesh->createEdge(v5,v6);
    }

    CommunicatedGeometry geo( offset, "SOLIDZMaster", "SOLIDZMaster", nullptr);
    geo.addReceiver("NASTIN", m2n);
    geo.prepare(*pSolidzMesh);
    geo.create(*pSolidzMesh);
    pSolidzMesh->computeDistribution();

    if(utils::Parallel::getProcessRank() == 2){//Slave1
      validate(pSolidzMesh->getVertexOffsets()[0]==2);
      validate(pSolidzMesh->getVertexOffsets()[1]==2);
      validate(pSolidzMesh->getVertexOffsets()[2]==6);
    }
    else if(utils::Parallel::getProcessRank() == 3){//Slave2
      validate(pSolidzMesh->getVertexOffsets()[0]==2);
      validate(pSolidzMesh->getVertexOffsets()[1]==2);
      validate(pSolidzMesh->getVertexOffsets()[2]==6);
    }


  }
  utils::MasterSlave::_slaveMode = false;
  utils::MasterSlave::_masterMode = false;
  utils::Parallel::synchronizeProcesses();
  utils::Parallel::clearGroups();
}


}}} // namespace precice, geometry, tests

#endif // PRECICE_NO_MPI
