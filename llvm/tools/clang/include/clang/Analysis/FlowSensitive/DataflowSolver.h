//===--- DataflowSolver.h - Skeleton Dataflow Analysis Code -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines skeleton code for implementing dataflow analyses.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_ANALYSES_DATAFLOW_SOLVER
#define LLVM_CLANG_ANALYSES_DATAFLOW_SOLVER
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/ProgramPoint.h"
#include "clang/Analysis/FlowSensitive/DataflowValues.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <clang/AST/Stmt.h>
#include <functional> // STL
#include <vector>
#include <queue>
#include <set>
#include <cstdio>
#define DEBUGBlock      0
#define DEBUGEdge       0
#define DEBUGMerge      0
#define DEBUGWiden		0
namespace clang
{
//===----------------------------------------------------------------------===//
/// DataflowWorkListTy - Data structure representing the worklist used for
///  dataflow algorithms.
//===----------------------------------------------------------------------===//
class DataflowWorkListTy
{
	llvm::DenseMap<const CFGBlock*, unsigned char> BlockSet;
	llvm::SmallVector<const CFGBlock *, 10> BlockQueue;
public:
	/// enqueue - Add a block to the worklist.  Blocks already on the
	///  worklist are not added a second time.
	void enqueue(const CFGBlock* B) {
		unsigned char &x = BlockSet[B];
		if ( x == 1 )
			return;
		x = 1;
		BlockQueue.push_back(B);
	}
	/// dequeue - Remove a block from the worklist.
	const CFGBlock* dequeue() {
		assert(!BlockQueue.empty());
		const CFGBlock *B = BlockQueue.back();
		BlockQueue.pop_back();
		BlockSet[B] = 0;
		return B;
	}
	/// isEmpty - Return true if the worklist is empty.
	bool isEmpty() const {
		return BlockQueue.empty();
	}
};
//===----------------------------------------------------------------------===//
// BlockItrTraits - Traits classes that allow transparent iteration
//  over successors/predecessors of a block depending on the direction
//  of our dataflow analysis.
//===----------------------------------------------------------------------===//
namespace dataflow
{
template<typename Tag> struct ItrTraits {
};
template <> struct ItrTraits<forward_analysis_tag> {
	typedef CFGBlock::const_pred_iterator PrevBItr;
	typedef CFGBlock::const_succ_iterator NextBItr;
	typedef CFGBlock::const_iterator      StmtItr;
	static PrevBItr PrevBegin(const CFGBlock* B) {
		return B->pred_begin();
	}
	static PrevBItr PrevEnd(const CFGBlock* B) {
		return B->pred_end();
	}
	static NextBItr NextBegin(const CFGBlock* B) {
		return B->succ_begin();
	}
	static NextBItr NextEnd(const CFGBlock* B) {
		return B->succ_end();
	}
	static StmtItr StmtBegin(const CFGBlock* B) {
		return B->begin();
	}
	static StmtItr StmtEnd(const CFGBlock* B) {
		return B->end();
	}
	static BlockEdge PrevEdge(const CFGBlock* B, const CFGBlock* Prev) {
		return BlockEdge(Prev, B, 0);
	}
	static BlockEdge NextEdge(const CFGBlock* B, const CFGBlock* Next) {
		return BlockEdge(B, Next, 0);
	}
};
template <> struct ItrTraits<backward_analysis_tag> {
	typedef CFGBlock::const_succ_iterator    PrevBItr;
	typedef CFGBlock::const_pred_iterator    NextBItr;
	typedef CFGBlock::const_reverse_iterator StmtItr;
	static PrevBItr PrevBegin(const CFGBlock* B) {
		return B->succ_begin();
	}
	static PrevBItr PrevEnd(const CFGBlock* B) {
		return B->succ_end();
	}
	static NextBItr NextBegin(const CFGBlock* B) {
		return B->pred_begin();
	}
	static NextBItr NextEnd(const CFGBlock* B) {
		return B->pred_end();
	}
	static StmtItr StmtBegin(const CFGBlock* B) {
		return B->rbegin();
	}
	static StmtItr StmtEnd(const CFGBlock* B) {
		return B->rend();
	}
	static BlockEdge PrevEdge(const CFGBlock* B, const CFGBlock* Prev) {
		return BlockEdge(B, Prev, 0);
	}
	static BlockEdge NextEdge(const CFGBlock* B, const CFGBlock* Next) {
		return BlockEdge(Next, B, 0);
	}
};
} // end namespace dataflow
//===----------------------------------------------------------------------===//
/// DataflowSolverTy - Generic dataflow solver.
//===----------------------------------------------------------------------===//
template <typename _DFValuesTy,      // Usually a subclass of DataflowValues
         typename _TransferFuncsTy,
         typename _MergeOperatorTy,
         typename _Equal = std::equal_to<typename _DFValuesTy::ValTy> >
class DataflowSolver
{
	//===----------------------------------------------------===//
	// Type declarations.
	//===----------------------------------------------------===//
public:
	typedef _DFValuesTy                              DFValuesTy;
	typedef _TransferFuncsTy                         TransferFuncsTy;
	typedef _MergeOperatorTy                         MergeOperatorTy;
	typedef typename _DFValuesTy::AnalysisDirTag     AnalysisDirTag;
	typedef typename _DFValuesTy::ValTy              ValTy;
	typedef typename _DFValuesTy::EdgeDataMapTy      EdgeDataMapTy;
	typedef typename _DFValuesTy::BlockDataMapTy     BlockDataMapTy;
	typedef dataflow::ItrTraits<AnalysisDirTag>      ItrTraits;
	typedef typename ItrTraits::NextBItr             NextBItr;
	typedef typename ItrTraits::PrevBItr             PrevBItr;
	typedef typename ItrTraits::StmtItr              StmtItr;
	//===----------------------------------------------------===//
	// External interface: constructing and running the solver.
	//===----------------------------------------------------===//
public:
	DataflowSolver(DFValuesTy& d) : D(d), TF(d.getAnalysisData()) {
	}
	~DataflowSolver() {
	}
	/// runOnCFG - Computes dataflow values for all blocks in a CFG.
	void runOnCFG(CFG& cfg, bool recordStmtValues = false) {
		// Set initial dataflow values and boundary conditions.
		D.InitializeValues(cfg);
		// Solve the dataflow equations.  This will populate D.EdgeDataMap
		// with dataflow values.
		SolveDataflowEquations(cfg, recordStmtValues);
	}
	/// runOnBlock - Computes dataflow values for a given block.  This
	///  should usually be invoked only after previously computing
	///  dataflow values using runOnCFG, as runOnBlock is intended to
	///  only be used for querying the dataflow values within a block
	///  with and Observer object.
	void runOnBlock(const CFGBlock* B, bool recordStmtValues) {
		BlockDataMapTy& M = D.getBlockDataMap();
		typename BlockDataMapTy::iterator I = M.find(B);
		if ( I != M.end() ) {
			TF.getVal().copyValues(I->second);
			ProcessBlock(B, recordStmtValues, AnalysisDirTag());
		}
	}
	void runOnBlock(const CFGBlock& B, bool recordStmtValues) {
		runOnBlock(&B, recordStmtValues);
	}
	void runOnBlock(CFG::iterator& I, bool recordStmtValues) {
		runOnBlock(*I, recordStmtValues);
	}
	void runOnBlock(CFG::const_iterator& I, bool recordStmtValues) {
		runOnBlock(*I, recordStmtValues);
	}
	void runOnAllBlocks(const CFG& cfg, bool recordStmtValues = false) {
		for ( CFG::const_iterator I=cfg.begin(), E=cfg.end(); I!=E; ++I )
			runOnBlock(I, recordStmtValues);
	}
	//===----------------------------------------------------===//
	// Internal solver logic.
	//===----------------------------------------------------===//
private:

	/// SolveDataflowEquations - Perform the actual worklist algorithm
	///  to compute dataflow values.
	void SolveDataflowEquations(CFG& cfg, bool recordStmtValues) {
		EnqueueBlocksOnWorklist(cfg, AnalysisDirTag());
		llvm::DenseMap<unsigned,unsigned> CounterMap;
		std::set<const CFGBlock *> VisitedBlocks;
		while ( !WorkList.isEmpty() ) {
			const CFGBlock* B = WorkList.dequeue();
			VisitedBlocks.insert(B);
			// save old out(B) for widening purposes
			ValTy VPre;
			if (D.getBlockDataMap().find(B) != D.getBlockDataMap().end())
				VPre = D.getBlockDataMap().find(B)->second;
			ProcessMerge(cfg, B);
			TF.getVal() = D.getBlockDataMap().find(B)->second;
#if (DEBUGBlock)
			fprintf(stderr,"\nProcessing block:\n");
			B->dump(&cfg,LangOptions());
			fprintf(stderr,"Visit Number %d\n.",CounterMap[B->getBlockID()]);
			fprintf(stderr,"in(B): ");
			TF.getVal().print();
#endif
			ProcessBlock(B, recordStmtValues, AnalysisDirTag());
			ValTy VPost = TF.getVal();
#if (DEBUGBlock)
			fprintf(stderr,"\nout(B): ");
			VPost.print();
			fprintf(stderr,"\n~out(B): ");
			TF.getNVal().print();
#endif

			// widen when reaching the threshold, according to widening point
			if ( ++CounterMap[B->getBlockID()] > TF.getVal().widening_threshold_ ) {
#if (DEBUGWiden)
				fprintf(stderr,"\nBlock (visited %d times):\n", CounterMap[B->getBlockID()]);
				B->dump(&cfg,LangOptions());
#endif
				if (TF.getVal().widening_point_ == 0/*AnalysisConfiguration::WIDEN_AT_ALL*/) {
#if (DEBUGWiden)
					fprintf(stderr,"\nStrategy: At-All\nWidning...\n");
#endif
					ValTy::Widening(VPre,VPost,TF.getVal());
#if (DEBUGWiden)
					fprintf(stderr,"\nResult:\n");
					TF.getVal().print();
#endif
				} else if (TF.getVal().widening_point_ == 2/*AnalysisConfiguration::WIDEN_AT_BACK_EDGE*/) {
#if (DEBUGWiden)
					fprintf(stderr,"\nStrategy: At-Back-Edge\n");
#endif
					// a block has a back edge if its predecessor id is greater than its own
					for ( PrevBItr I=ItrTraits::PrevBegin(B),E=ItrTraits::PrevEnd(B); I!=E; ++I ) {
						CFGBlock *PrevBlk = *I;
						if ( PrevBlk && PrevBlk->getBlockID() < B->getBlockID() ) {
#if (DEBUGWiden)
							fprintf(stderr,"\nBack Edge Found! (%d), Widneing...\n",PrevBlk->getBlockID());
#endif
							ValTy::Widening(VPre,VPost,TF.getVal());
#if (DEBUGWiden)
							fprintf(stderr,"\nResult:\n");
							TF.getVal().print();
#endif
							break;
						}
					}
				} else if (TF.getVal().widening_point_ == 1/*AnalysisConfiguration::WIDEN_AT_CORR_POINT*/ && TF.getVal().at_diff_point_ == true) {
					   TF.getVal().at_diff_point_ = false;
#if (DEBUGWiden)
						fprintf(stderr,"\nDiff Point Found! (%d), Widneing...\n");
#endif
						ValTy::Widening(VPre,VPost,TF.getVal());
#if (DEBUGWiden)
						fprintf(stderr,"\nResult:\n");
						TF.getVal().print();
#endif
				}
			}
#if (DEBUGBlock)
			getchar();
#endif
			UpdateEdges(cfg, B, TF.getNVal(), TF.getVal());
		}
	}
	void EnqueueBlocksOnWorklist(CFG &cfg, dataflow::forward_analysis_tag) {
		// Enqueue all blocks to ensure the dataflow values are computed
		// for every block.  Not all blocks are guaranteed to reach the exit block.
		for ( CFG::iterator I=cfg.begin(), E=cfg.end(); I!=E; ++I )
			WorkList.enqueue(&**I);
	}
	void EnqueueBlocksOnWorklist(CFG &cfg, dataflow::backward_analysis_tag) {
		// Enqueue all blocks to ensure the dataflow values are computed
		// for every block.  Not all blocks are guaranteed to reach the exit block.
		// Enqueue in reverse order since that will more likely match with
		// the order they should ideally processed by the dataflow algorithm.
		for ( CFG::reverse_iterator I=cfg.rbegin(), E=cfg.rend(); I!=E; ++I )
			WorkList.enqueue(&**I);
	}
	void ProcessMerge(CFG& cfg, const CFGBlock* B) {
		ValTy V;
		//TF.SetTopValue(V);
		//TF.SetTopValue(TF.getNVal());
		// Merge dataflow values from all predecessors of this block.
		MergeOperatorTy Merge;
		EdgeDataMapTy& M = D.getEdgeDataMap();
		bool firstMerge = true;
		bool noEdges = true;
#if (DEBUGMerge)
		fprintf(stderr,"\n-----------\nMergging: ");
#endif
		for ( PrevBItr I=ItrTraits::PrevBegin(B),E=ItrTraits::PrevEnd(B); I!=E; ++I ) {
			CFGBlock *PrevBlk = *I;
			if ( !PrevBlk )
				continue;
			typename EdgeDataMapTy::iterator EI = M.find(ItrTraits::PrevEdge(B, PrevBlk));
			if ( EI != M.end() ) {
				noEdges = false;
				if ( firstMerge ) {
					firstMerge = false;
#if (DEBUGMerge)
					fprintf(stderr,"\nfrom: ");
					PrevBlk->dump(&cfg,LangOptions());
					EI->second.print();
#endif
					V.copyValues(EI->second);
				} else {
#if (DEBUGMerge)
					fprintf(stderr,"\nfrom: ");
					PrevBlk->dump(&cfg,LangOptions());
					EI->second.print();
#endif
					Merge(V, EI->second);
				}
			}
		}
		bool isInitialized = true;
		typename BlockDataMapTy::iterator BI = D.getBlockDataMap().find(B);
		if ( BI == D.getBlockDataMap().end() ) {
			isInitialized = false;
			// We want the new ValTy to have the same environment as before
			// that's why we initialize it with V (same environment, top abstract state) and not with
			// a new ValTy()
			BI = D.getBlockDataMap().insert( std::make_pair(B,V) ).first;
		}
		// If no edges have been found, it means this is the first time the solver
		// has been called on block B, we copy the initialization values (if any)
		// as current value for V (which will be used as edge data)
		if ( noEdges && isInitialized ) {
			Merge(V, BI->second);
		}
		// Set the data for the block.
		BI->second.copyValues(V);
		// partioning at join may happen only here!
		if ( BI->second.partition_point_ == 1/*AnalysisConfiguration::PARTITION_AT_JOIN*/ ) {
			BI->second.Partition();
		}
#if (DEBUGMerge)
		fprintf(stderr,"\nResult:");
		BI->second.print();
		fprintf(stderr,"\n-----------");
		getchar();
#endif
	}
	/// ProcessBlock - Process the transfer functions for a given block.
	void ProcessBlock(const CFGBlock* B, bool recordStmtValues,
	                  dataflow::forward_analysis_tag) {
		for ( StmtItr I=ItrTraits::StmtBegin(B), E=ItrTraits::StmtEnd(B); I!=E; ++I ) {
			CFGElement El = *I;
			if ( const CFGStmt *S = El.getAs<CFGStmt>() )
				ProcessStmt(S->getStmt(), recordStmtValues, AnalysisDirTag());
		}
		TF.VisitTerminator(const_cast<CFGBlock*>(B));
	}
	void ProcessBlock(const CFGBlock* B, bool recordStmtValues,
	                  dataflow::backward_analysis_tag) {
		TF.VisitTerminator(const_cast<CFGBlock*>(B));
		for ( StmtItr I=ItrTraits::StmtBegin(B), E=ItrTraits::StmtEnd(B); I!=E; ++I ) {
			CFGElement El = *I;
			if ( CFGStmt S = El.getAs<CFGStmt>() )
				ProcessStmt(S, recordStmtValues, AnalysisDirTag());
		}
	}
	void ProcessStmt(const Stmt* S, bool record, dataflow::forward_analysis_tag) {
		if ( record ) D.getStmtDataMap()[S] = TF.getVal();
		TF.BlockStmt_Visit(const_cast<Stmt*>(S));
	}
	void ProcessStmt(const Stmt* S, bool record, dataflow::backward_analysis_tag) {
		TF.BlockStmt_Visit(const_cast<Stmt*>(S));
		if ( record ) D.getStmtDataMap()[S] = TF.getVal();
	}
	/// UpdateEdges - After processing the transfer functions for a
	///   block, update the dataflow value associated with the block's
	///   outgoing/incoming edges (depending on whether we do a
	///    forward/backward analysis respectively)
	/// In case this block is a conditional, we update the negated edge to be NV
	///   which holds the negation of the conditional
	void UpdateEdges(CFG& cfg, const CFGBlock* B, ValTy& NV, ValTy& V) {
		/*
		for (NextBItr I=ItrTraits::NextBegin(B), E=ItrTraits::NextEnd(B); I!=E; ++I)
			if (CFGBlock *NextBlk = *I)
				UpdateEdgeValue(cfg, ItrTraits::NextEdge(B, NextBlk),V, NextBlk);
		*/
		CFGBlock * FirstSucc = 0, *LastSucc = 0;
		for ( NextBItr I=ItrTraits::NextBegin(B), E=ItrTraits::NextEnd(B); I!=E; ++I ) {
			if ( CFGBlock *NextBlk = *I ) {
			   if ( !FirstSucc )
				  FirstSucc = NextBlk;
			   LastSucc = NextBlk;
			}
		}
		if (!FirstSucc) { // No edges to update.
			return;
		}
		UpdateEdgeValue(cfg, ItrTraits::NextEdge(B, FirstSucc),V, FirstSucc);
		if ( LastSucc != FirstSucc ) { // This means it's a conditional
			UpdateEdgeValue(cfg, ItrTraits::NextEdge(B, LastSucc),NV, LastSucc);
		}
	}
	/// UpdateEdgeValue - Update the value associated with a given edge.
	void UpdateEdgeValue(const CFG &cfg, BlockEdge E, ValTy& V, const CFGBlock* TargetBlock) {
		EdgeDataMapTy& M = D.getEdgeDataMap();
		//fprintf(stderr,"EdgeDataMap Size: %d, Overall State Size: %d\n",M.size(), M.begin()->second.size());
		typename EdgeDataMapTy::iterator I = M.find(E);
		if ( I == M.end() ) {  // First computed value for this edge?
#if (DEBUGEdge)
			fprintf(stderr,"enqueuing(B):\n");
			TargetBlock->print(llvm::errs(),&cfg,LangOptions());
#endif
			M[E].copyValues(V);
			WorkList.enqueue(TargetBlock);
		} else if ( !_Equal()(V,I->second) ) {
#if (DEBUGEdge)
			fprintf(stderr,"enqueuing(B):\n");
			TargetBlock->print(llvm::errs(),&cfg,LangOptions());
#endif
			I->second.copyValues(V);
			WorkList.enqueue(TargetBlock);
		}
	}
private:
	DFValuesTy& D;
	DataflowWorkListTy WorkList;
	TransferFuncsTy TF;
};
} // end namespace clang
#endif
