#ifndef PRECICE_NO_MPI
#ifndef PARALLELMATRIXOPERATIONS_HPP_
#define PARALLELMATRIXOPERATIONS_HPP_

#include "logging/Logger.hpp"
#include "com/MPIPortsCommunication.hpp"
#include "com/Communication.hpp"
#include "utils/MasterSlave.hpp"
#include "utils/Parallel.hpp"
#include "utils/Globals.hpp"
#include <Eigen/Dense>

namespace precice {
namespace cplscheme {
namespace impl {

class ParallelMatrixOperations
{
public:

  /**
   * @brief Constructor.
   */
	ParallelMatrixOperations ();

   /**
    * @brief Destructor, empty.
    */
   virtual ~ParallelMatrixOperations(){};


   /**
    * @brief Initializes the post-processing.
    */
   void initialize(com::Communication::SharedPointer leftComm,
		   	   	   com::Communication::SharedPointer rightComm,
		   	   	   bool needcyclicComm);

   template<typename Derived1, typename Derived2>
   void multiply(
       Eigen::PlainObjectBase<Derived1>& leftMatrix,
		   Eigen::PlainObjectBase<Derived2>& rightMatrix,
		   Eigen::PlainObjectBase<Derived2>& result,
   		 const std::vector<int>& offsets,
   		 int p, int q, int r,
   		 bool dotProductComputation = true)
   {
		preciceTrace("multiply()");
		assertion(result.cols() == rightMatrix.cols(), result.cols(), rightMatrix.cols());
		assertion(leftMatrix.cols() == rightMatrix.rows(), leftMatrix.cols(), rightMatrix.rows());

		// if serial computation on single processor, i.e, no master-slave mode
		if( not utils::MasterSlave::_masterMode && not utils::MasterSlave::_slaveMode){
			result.noalias() = leftMatrix * rightMatrix;

		// if parallel computation on p processors, i.e., master-slave mode
		}else{
			assertion(utils::MasterSlave::_communication.get() != NULL);
			assertion(utils::MasterSlave::_communication->isConnected());

			// The result matrix is of size (p x r)
			// if p equals r (and p = global_n), we have to perform the
			// cyclic communication with block-wise matrix-matrix multiplication
			if(p == r){
			  assertion(_needCycliclComm);
				assertion(_cyclicCommLeft.get() != NULL); assertion(_cyclicCommLeft->isConnected());
				assertion(_cyclicCommRight.get() != NULL); assertion(_cyclicCommRight->isConnected());

				_multiplyNN(leftMatrix, rightMatrix, result, offsets, p, q, r);

			// case p != r, i.e., usually p = number of columns of the least squares system
			// perform parallel multiplication based on dot-product
			}else{
				if(dotProductComputation)
					_multiplyNM_dotProduct(leftMatrix, rightMatrix, result, offsets, p, q, r);
				else
					_multiplyNM_block(leftMatrix, rightMatrix, result, offsets, p, q, r);
			}
		}
   }

   /** @brief: Method computes the matrix-matrix/matrix-vector product of a (p x q)
    * matrix that is distributed column-wise (e.g. pseudoInverse Z), with a matrix/vector
    * of size (q x r) with r=1/cols, that is distributed row-wise (e.g. _matrixW, _matrixV, residual).
    *
    * In each case mat-mat or mat-vec product, the result is of size (m x m) or (m x 1), where
    * m is the number of cols, i.e., small such that the result is stored on each proc.
    *
    * @param[in] p - first dimension, i.e., overall (global) number of rows
    * @param[in] q - inner dimension
    * @param[in] r - second dimension, i.e., overall (global) number cols of result matrix
    *
    */
   template<typename Derived1, typename Derived2, typename Derived3>
    void multiply(
        const Eigen::MatrixBase<Derived1>& leftMatrix,
        const Eigen::MatrixBase<Derived2>& rightMatrix,
        Eigen::PlainObjectBase<Derived3>& result,
        int p, int q, int r)
  {
    preciceTrace("multiply() (m x n) * (n * r), r=1/m");
    assertion(leftMatrix.rows() == p, leftMatrix.rows(), p);
    assertion(leftMatrix.cols() == rightMatrix.rows(),leftMatrix.cols(), rightMatrix.rows());
    assertion(result.rows() == p, result.rows(), p);
    assertion(result.cols() == r, result.cols(), r);

    Eigen::MatrixXd localResult(result.rows(), result.cols());
    localResult.noalias() = leftMatrix * rightMatrix;

    // if serial computation on single processor, i.e, no master-slave mode
    if( not utils::MasterSlave::_masterMode && not utils::MasterSlave::_slaveMode){
      result = localResult;
    }else{
      utils::MasterSlave::allreduceSum(localResult.data(), result.data(), localResult.size());
    }
  }


private:

   // @brief Logging device.
   static logging::Logger _log;

  // @brief multiplies matrices based on a cyclic communication and block-wise matrix multiplication with a quadratic result matrix
   template<typename Derived1, typename Derived2>
   void _multiplyNN(
       Eigen::PlainObjectBase<Derived1>& leftMatrix,
		   Eigen::PlainObjectBase<Derived2>& rightMatrix,
		   Eigen::PlainObjectBase<Derived2>& result,
		   const std::vector<int>& offsets,
		   int p, int q, int r)
   {
     preciceTrace("multiplyNN() (n x m) * (m x n)");
		/*
		 * For multiplication W_til * Z = J
		 * -----------------------------------------------------------------------
		 * p = r = n_global, q = m
		 *
		 * leftMatrix:  local: (n_local x m) 		global: (n_global x m)
		 * rightMatrix: local: (m x n_local) 		global: (m x n_global)
		 * result: 		local: (n_global x n_local) global: (n_global x n_global)
		 * -----------------------------------------------------------------------
		 */

    assertion(_needCycliclComm);
		assertion(leftMatrix.cols() == q, leftMatrix.cols(), q);
		assertion(leftMatrix.rows() == rightMatrix.cols(), leftMatrix.rows(), rightMatrix.cols());
		assertion(result.rows() == p, result.rows(), p);

		//int nextProc = (utils::MasterSlave::_rank + 1) % utils::MasterSlave::_size;
		int prevProc = (utils::MasterSlave::_rank -1 < 0) ? utils::MasterSlave::_size-1 : utils::MasterSlave::_rank -1;
		int rows_rcv = (prevProc > 0) ? offsets[prevProc+1] - offsets[prevProc] : offsets[1];
		//Eigen::MatrixXd leftMatrix_rcv = Eigen::MatrixXd::Zero(rows_rcv, q);
		Eigen::MatrixXd leftMatrix_rcv(rows_rcv, q);

		com::Request::SharedPointer requestSend;
		com::Request::SharedPointer requestRcv;

		// initiate asynchronous send operation of leftMatrix (W_til) --> nextProc (this data is needed in cycle 1)    dim: n_local x cols
		if(leftMatrix.size() > 0)
			requestSend = _cyclicCommRight->aSend(leftMatrix.data(), leftMatrix.size(), 0);

		// initiate asynchronous receive operation for leftMatrix (W_til) from previous processor --> W_til      dim: rows_rcv x cols
		if(leftMatrix_rcv.size() > 0)
			requestRcv = _cyclicCommLeft->aReceive(leftMatrix_rcv.data(), leftMatrix_rcv.size(), 0);

		// compute diagonal blocks where all data is local and no communication is needed
		// compute block matrices of J_inv of size (n_til x n_til), n_til = local n
		Eigen::MatrixXd diagBlock(leftMatrix.rows(), leftMatrix.rows());
		diagBlock.noalias() = leftMatrix * rightMatrix;

		// set block at corresponding row-index on proc
		int off = offsets[utils::MasterSlave::_rank];
		assertion(result.cols() == diagBlock.cols(), result.cols(), diagBlock.cols());
		result.block(off, 0, diagBlock.rows(), diagBlock.cols()) = diagBlock;

		/**
		 * cyclic send-receive operation
		 */
		for(int cycle = 1; cycle < utils::MasterSlave::_size; cycle++){

			// wait until W_til from previous processor is fully received
			if(requestSend != NULL) requestSend->wait();
			if(requestRcv != NULL)  requestRcv->wait();

			// leftMatrix (leftMatrix_rcv) is available - needed for local multiplication and hand over to next proc
			Eigen::MatrixXd leftMatrix_copy(leftMatrix_rcv);

			// initiate async send to hand over leftMatrix (W_til) to the next proc (this data will be needed in the next cycle)    dim: n_local x cols
			if(cycle < utils::MasterSlave::_size-1){
			  if(leftMatrix_copy.size() > 0)
				  requestSend = _cyclicCommRight->aSend(leftMatrix_copy.data(), leftMatrix_copy.size(), 0);
			}

			// compute proc that owned leftMatrix_rcv (Wtil_rcv) at the very beginning for each cylce
			int sourceProc_nextCycle = (utils::MasterSlave::_rank - (cycle+1) < 0) ?
				  utils::MasterSlave::_size + (utils::MasterSlave::_rank - (cycle+1)) : utils::MasterSlave::_rank - (cycle+1);

			int sourceProc = (utils::MasterSlave::_rank - cycle < 0) ?
				  utils::MasterSlave::_size + (utils::MasterSlave::_rank - cycle) : utils::MasterSlave::_rank - cycle;

			int rows_rcv_nextCycle = (sourceProc_nextCycle > 0) ? offsets[sourceProc_nextCycle+1] - offsets[sourceProc_nextCycle] : offsets[1];
			rows_rcv = (sourceProc > 0) ? offsets[sourceProc+1] - offsets[sourceProc] : offsets[1];
			leftMatrix_rcv = Eigen::MatrixXd::Zero(rows_rcv_nextCycle, q);


			// initiate asynchronous receive operation for leftMatrix (W_til) from previous processor --> W_til (this data is needed in the next cycle)
			if(cycle < utils::MasterSlave::_size-1){
			  if(leftMatrix_rcv.size() > 0) // only receive data, if data has been sent
				  requestRcv = _cyclicCommLeft->aReceive(leftMatrix_rcv.data(), leftMatrix_rcv.size(), 0);
			}

			if(requestSend != NULL) requestSend->wait();
			// compute block with new local data
			Eigen::MatrixXd block(rows_rcv, rightMatrix.cols());
			block.noalias() = leftMatrix_copy * rightMatrix;

			// set block at corresponding index in J_inv
			// the row-offset of the current block is determined by the proc that sends the part of the W_til matrix
			// note: the direction and ordering of the cyclic sending operation is chosen s.t. the computed block is
			//       local on the current processor (in J_inv).
			off = offsets[sourceProc];
			assertion(result.cols() == block.cols(), result.cols(), block.cols());
			result.block(off, 0, block.rows(), block.cols()) = block;
		}
   }


   // @brief multiplies matrices based on a dot-product computation with a rectangular result matrix
   template<typename Derived1, typename Derived2>
   void _multiplyNM_dotProduct(
       Eigen::PlainObjectBase<Derived1>& leftMatrix,
		   Eigen::PlainObjectBase<Derived2>& rightMatrix,
		   Eigen::PlainObjectBase<Derived2>& result,
		   const std::vector<int>& offsets,
		   int p, int q, int r)
   {
     preciceTrace("multiplyNM() (n x n) * (n x m)");
		for(int i = 0; i < leftMatrix.rows(); i++){
		  int rank = 0;
		  // find rank of processor that stores the result
		  // the second while is necessary if processors with no vertices are present
		  // Note: the >'=' here is crucial: In case some procs do not have any vertices,
		  // this while loop continues incrementing rank if entries in offsets are equal, i.e.,
		  // it runs to the next non-empty proc.
		  while(i >= offsets[rank+1]) rank++;

		  Eigen::VectorXd lMRow = leftMatrix.row(i);

		  for(int j = 0; j < r; j++){

			  Eigen::VectorXd rMCol = rightMatrix.col(j);
			  double res_ij = utils::MasterSlave::dot(lMRow, rMCol);

			  // find proc that needs to store the result.
			  int local_row;
			  if(utils::MasterSlave::_rank == rank)
			  {
				  local_row = i - offsets[rank];
				  result(local_row, j) = res_ij;
			  }
		  }
		}
   }

	// @brief multiplies matrices based on a SAXPY-like block-wise computation with a rectangular result matrix of dimension n x m
	template<typename Derived1, typename Derived2>
	void _multiplyNM_block(
	    Eigen::PlainObjectBase<Derived1>& leftMatrix,
			Eigen::PlainObjectBase<Derived2>& rightMatrix,
		  Eigen::PlainObjectBase<Derived2>& result,
			const std::vector<int>& offsets,
			int p, int q, int r)
	{
	  preciceTrace("multiplyNM_block() (n x n) * (n x m)");

		// ensure that both matrices are stored in the same order. Important for reduce function, that adds serialized data.
		assertion(leftMatrix.IsRowMajor == rightMatrix.IsRowMajor, leftMatrix.IsRowMajor, rightMatrix.IsRowMajor);

		// multiply local block (saxpy-based approach)
		// dimension: (n_global x n_local) * (n_local x m) = (n_global x m)
		Eigen::MatrixXd block = Eigen::MatrixXd::Zero(p, r);
		block.noalias() = leftMatrix * rightMatrix;

		// all blocks have size (n_global x m)
		// Note: if procs have no vertices, the block size remains (n_global x m), however,
		// 	     it must be initialized with zeros, so zeros are added for those procs)

		// sum up blocks in master, reduce
		Eigen::MatrixXd summarizedBlocks = Eigen::MatrixXd::Zero(p, r); // TODO: only master should allocate memory.
		utils::MasterSlave::reduceSum(block.data(), summarizedBlocks.data(), block.size());

		// slaves wait to receive their local result
		if(utils::MasterSlave::_slaveMode){
      if(result.size() > 0)
        utils::MasterSlave::_communication->receive(result.data(), result.size(), 0);
    }

		// master distributes the sub blocks of the results
		if(utils::MasterSlave::_masterMode){
			// distribute blocks of summarizedBlocks (result of multiplication) to corresponding slaves
			result = summarizedBlocks.block(0, 0, offsets[1], r);

			for(int rankSlave = 1; rankSlave <  utils::MasterSlave::_size; rankSlave++){
				int off = offsets[rankSlave];
				int send_rows = offsets[rankSlave+1] - offsets[rankSlave];

				if(summarizedBlocks.block(off, 0, send_rows, r).size() > 0){
					// necessary to save the matrix-block that is to be sent in a temporary matrix-object
					// otherwise, the send routine walks over the bounds of the block (matrix structure is still from the entire matrix)
					Eigen::MatrixXd sendBlock = summarizedBlocks.block(off, 0, send_rows, r);
					utils::MasterSlave::_communication->send(sendBlock.data(),sendBlock.size(), rankSlave);
				}
			}
		}
	}


	/**
	* @brief Communication between neighboring slaves, backwards
	*/
	com::Communication::SharedPointer _cyclicCommLeft;

	/**
	* @brief Communication between neighboring slaves, forward
	*/
	com::Communication::SharedPointer _cyclicCommRight;

	bool _needCycliclComm;

};

}}} // namespace precice, cplscheme, impl


#endif /* PARALLELMATRIXOPERATIONS_HPP_ */
#endif
