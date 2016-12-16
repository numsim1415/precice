#ifndef PRECICE_NO_MPI

#ifndef PRECICE_COM_MPI_PORTS_COMMUNICATION_HPP_
#define PRECICE_COM_MPI_PORTS_COMMUNICATION_HPP_

#include "MPICommunication.hpp"

#include "logging/Logger.hpp"

#include <vector>

namespace precice {
namespace com {
/**
 * @brief Provides connection methods based on MPI ports (part of MPI 2.0).
 *
 * The two participants to be connected can be run in two process groups started
 * up individually, i.e. not within the same process group.
 */
class MPIPortsCommunication : public MPICommunication {
public:
  explicit MPIPortsCommunication(std::string const& addressDirectory = ".");

  virtual ~MPIPortsCommunication();

  /// Returns true, if a connection to a remote participant has been setup.
  virtual bool isConnected();

  /**
   * @brief Returns the number of processes in the remote communicator.
   *
   * Precondition: a connection to the remote participant has been setup.
   */
  virtual size_t getRemoteCommunicatorSize();

  /// See precice::com::Communication::acceptConnection().
  virtual void acceptConnection(std::string const& nameAcceptor,
                                std::string const& nameRequester,
                                int acceptorProcessRank,
                                int acceptorCommunicatorSize);

  virtual void acceptConnectionAsServer(std::string const& nameAcceptor,
                                        std::string const& nameRequester,
                                        int requesterCommunicatorSize);

  /// See precice::com::Communication::requestConnection().
  virtual void requestConnection(std::string const& nameAcceptor,
                                 std::string const& nameRequester,
                                 int requesterProcessRank,
                                 int requesterCommunicatorSize);

  virtual int requestConnectionAsClient(std::string const& nameAcceptor,
                                        std::string const& nameRequester);

  /**
   * @brief See precice::com::Communication::closeConnection().
   */
  virtual void closeConnection();

private:
  virtual MPI_Comm& communicator(int rank);

  virtual int rank(int rank);

  static logging::Logger _log;

  std::string _addressDirectory;

  std::vector<MPI_Comm> _communicators;

  /// Name of the port used for connection.
  char _portName[MPI_MAX_PORT_NAME];

  bool _isAcceptor;

  /// Flag indicating a connection.
  bool _isConnected;
};
}
} // namespace precice, com

#endif /* PRECICE_COM_MPI_PORTS_COMMUNICATION_HPP_ */

#endif // not PRECICE_NO_MPI
