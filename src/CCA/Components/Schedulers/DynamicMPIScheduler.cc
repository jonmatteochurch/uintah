/*
 * The MIT License
 *
 * Copyright (c) 1997-2016 The University of Utah
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <CCA/Components/Schedulers/DynamicMPIScheduler.h>
#include <CCA/Components/Schedulers/OnDemandDataWarehouse.h>
#include <CCA/Components/Schedulers/DetailedTask.h>
#include <CCA/Components/Schedulers/DetailedTasks.h>
#include <CCA/Components/Schedulers/TaskGraph.h>

#include <Core/Exceptions/ProblemSetupException.h>
#include <Core/Util/DOUT.hpp>
#include <Core/Util/Time.h>


using namespace Uintah;


extern Dout g_task_dbg;
extern Dout g_task_order;
extern Dout g_exec_out;


namespace {

Dout g_dbg(          "DynamicMPI_DBG",         false);
Dout g_queue_length( "DynamicMPI_QueueLength", false);
Dout g_timeout(      "DynamicMPI_TimingsOut",  false);

}


//______________________________________________________________________
//
DynamicMPIScheduler::DynamicMPIScheduler( const ProcessorGroup*      myworld,
                                          const Output*              oport,
                                                DynamicMPIScheduler* parentScheduler )
  : MPIScheduler( myworld, oport, parentScheduler )
{
  m_task_queue_alg =  MostMessages;

  if (g_timeout) {
    char filename[64];
    sprintf(filename, "timingStats.%d", d_myworld->myrank());
    m_timings_stats.open(filename);
    if (d_myworld->myrank() == 0) {
      sprintf(filename, "timingStats.avg");
      m_avg_stats.open(filename);
      sprintf(filename, "timingStats.max");
      m_max_stats.open(filename);
    }
  }
}

//______________________________________________________________________
//
DynamicMPIScheduler::~DynamicMPIScheduler()
{
  if (g_timeout) {
    m_timings_stats.close();
    if (d_myworld->myrank() == 0) {
      m_avg_stats.close();
      m_max_stats.close();
    }
  }
}

//______________________________________________________________________
//
void
DynamicMPIScheduler::problemSetup( const ProblemSpecP&     prob_spec,
                                         SimulationStateP& state )
{
  std::string taskQueueAlg = "";

  ProblemSpecP params = prob_spec->findBlock("Scheduler");
  if (params) {
    params->get("taskReadyQueueAlg", taskQueueAlg);
  }
  if (taskQueueAlg == "") {
    taskQueueAlg = "MostMessages";  //default taskReadyQueueAlg
  }

  if (taskQueueAlg == "FCFS") {
    m_task_queue_alg = FCFS;
  }
  else if (taskQueueAlg == "Random") {
    m_task_queue_alg = Random;
  }
  else if (taskQueueAlg == "Stack") {
    m_task_queue_alg = Stack;
  }
  else if (taskQueueAlg == "MostChildren") {
    m_task_queue_alg = MostChildren;
  }
  else if (taskQueueAlg == "LeastChildren") {
    m_task_queue_alg = LeastChildren;
  }
  else if (taskQueueAlg == "MostAllChildren") {
    m_task_queue_alg = MostChildren;
  }
  else if (taskQueueAlg == "LeastAllChildren") {
    m_task_queue_alg = LeastChildren;
  }
  else if (taskQueueAlg == "MostL2Children") {
    m_task_queue_alg = MostL2Children;
  }
  else if (taskQueueAlg == "LeastL2Children") {
    m_task_queue_alg = LeastL2Children;
  }
  else if (taskQueueAlg == "MostMessages") {
    m_task_queue_alg = MostMessages;
  }
  else if (taskQueueAlg == "LeastMessages") {
    m_task_queue_alg = LeastMessages;
  }
  else if (taskQueueAlg == "PatchOrder") {
    m_task_queue_alg = PatchOrder;
  }
  else if (taskQueueAlg == "PatchOrderRandom") {
    m_task_queue_alg = PatchOrderRandom;
  }
  else {
    throw ProblemSetupException("Unknown task ready queue algorithm", __FILE__, __LINE__);
  }

  SchedulerCommon::problemSetup(prob_spec, state);
}

//______________________________________________________________________
//
SchedulerP
DynamicMPIScheduler::createSubScheduler()
{
  UintahParallelPort  * lbp      = getPort("load balancer");
  DynamicMPIScheduler * newsched = scinew DynamicMPIScheduler( d_myworld, m_out_port, this );
  newsched->m_shared_state = m_shared_state;
  newsched->attachPort( "load balancer", lbp );
  newsched->m_shared_state = m_shared_state;
  return newsched;
}

//______________________________________________________________________
//
void
DynamicMPIScheduler::execute( int tgnum     /*=0*/,
                              int iteration /*=0*/ )
{
  if (m_shared_state->isCopyDataTimestep()) {
    MPIScheduler::execute(tgnum, iteration);
    return;
  }

  ASSERTRANGE(tgnum, 0, static_cast<int>(m_task_graphs.size()));
  TaskGraph* tg = m_task_graphs[tgnum];
  tg->setIteration(iteration);
  m_current_task_graph = tgnum;

  if (static_cast<int>(m_task_graphs.size()) > 1) {
    // tg model is the multi TG model, where each graph is going to need to
    // have its dwmap reset here (even with the same tgnum)
    tg->remapTaskDWs(m_dwmap);
  }

  DetailedTasks* dts = tg->getDetailedTasks();
  
  int ntasks = dts->numLocalTasks();
  dts->initializeScrubs(m_dws, m_dwmap);
  dts->initTimestep();

  for (int i = 0; i < ntasks; i++) {
    dts->localTask(i)->resetDependencyCounts();
  }

  if(g_timeout) {
    m_labels.clear();
    m_times.clear();
    emitTime("time since last execute");
  }

  int me = d_myworld->myrank();

  // This only happens if "-emit_taskgraphs" is passed to sus
  makeTaskGraphDoc(dts, me);

  mpi_info_.reset( 0 );

  if( m_reloc_new_pos_label && m_dws[m_dwmap[Task::OldDW]] != nullptr ) {
    m_dws[m_dwmap[Task::OldDW]]->exchangeParticleQuantities(dts, getLoadBalancer(), m_reloc_new_pos_label, iteration);
  }

#if 0
  // hook to post all the messages up front
  if (!m_shared_state->isCopyDataTimestep()) {
    // post the receives in advance
    for (int i = 0; i < ntasks; i++) {
      initiateTask( dts->localTask(i), abort, abort_point, iteration );
    }
  }
#endif

  int currphase = 0;
  std::map<int, int> phaseTasks;
  std::map<int, int> phaseTasksDone;
  std::map<int,  DetailedTask *> phaseSyncTask;
  dts->setTaskPriorityAlg(m_task_queue_alg);

  for (int i = 0; i < ntasks; i++) {
    phaseTasks[dts->localTask(i)->getTask()->m_phase]++;
  }
  
  if (g_dbg) {
    std::ostringstream message;
    message << "Rank-" << me << " Executing " << dts->numTasks() << " tasks (" << ntasks << " local)";
    for (std::map<int, int>::iterator it = phaseTasks.begin(); it != phaseTasks.end(); it++) {
      message << ", phase[" << (*it).first << "] = " << (*it).second;
    }
    DOUT(true, message.str());
  }

  static std::vector<int> histogram;
  static int totaltasks;
  std::set<DetailedTask*> pending_tasks;

  int numTasksDone = 0;
  bool abort       = false;
  int  abort_point = 987654;
  int i            = 0;

  while( numTasksDone < ntasks ) {

    i++;

    DetailedTask * task = nullptr;

    // if we have an internally-ready task, initiate its recvs
    while(dts->numInternalReadyTasks() > 0) { 
      DetailedTask * task = dts->getNextInternalReadyTask();

      if ((task->getTask()->getType() == Task::Reduction) || (task->getTask()->usesMPI())) {  //save the reduction task for later
        phaseSyncTask[task->getTask()->m_phase] = task;
        DOUT(g_task_dbg, "Rank-" << d_myworld->myrank() << " Task Reduction ready " << *task << " deps needed: " << task->getExternalDepCount());
      } else {
        initiateTask(task, abort, abort_point, iteration);
        task->markInitiated();
        task->checkExternalDepCount();
        DOUT(g_task_dbg, "Rank-" << d_myworld->myrank() << " Task internal ready " << *task << " deps needed: " << task->getExternalDepCount());

        // if MPI has completed, it will run on the next iteration
        pending_tasks.insert(task);
      }
    }

    if (dts->numExternalReadyTasks() > 0) {
      // run a task that has its communication complete
      // tasks get in this queue automatically when their receive count hits 0
      //   in DependencyBatch::received, which is called when a message is delivered.
      if (g_queue_length) {
        if ((int)histogram.size() < dts->numExternalReadyTasks() + 1) {
          histogram.resize(dts->numExternalReadyTasks() + 1);
        }
        histogram[dts->numExternalReadyTasks()]++;
      }
     
      DetailedTask * task = dts->getNextExternalReadyTask();

      DOUT(g_task_dbg,
           "Rank-" << d_myworld->myrank() << " Running task " << *task << "(" << dts->numExternalReadyTasks() << "/" << pending_tasks.size() << " tasks in queue)");;

      pending_tasks.erase(pending_tasks.find(task));
      ASSERTEQ(task->getExternalDepCount(), 0);
      runTask(task, iteration);
      numTasksDone++;

      if (g_task_order && d_myworld->myrank() == d_myworld->size() / 2) {
        DOUT(true, d_myworld->myrank() << " Running task static order: " << task->getStaticOrder() << " , scheduled order: " << numTasksDone);
      }
      phaseTasksDone[task->getTask()->m_phase]++;
    } 

    if ((phaseSyncTask.find(currphase) != phaseSyncTask.end()) && (phaseTasksDone[currphase] == phaseTasks[currphase] - 1)) {  //if it is time to run the reduction task
      if (g_queue_length) {
        if ((int)histogram.size() < dts->numExternalReadyTasks() + 1) {
          histogram.resize(dts->numExternalReadyTasks() + 1);
        }
        histogram[dts->numExternalReadyTasks()]++;
      }
      DetailedTask *reducetask = phaseSyncTask[currphase];
      if (reducetask->getTask()->getType() == Task::Reduction) {
        if (!abort) {
          DOUT(g_task_dbg, "Rank-" << d_myworld->myrank() << " Running Reduce task " << reducetask->getTask()->getName());
        }
        initiateReduction(reducetask);
      }
      else {  // Task::OncePerProc task
        ASSERT(reducetask->getTask()->usesMPI());
        initiateTask(reducetask, abort, abort_point, iteration);
        reducetask->markInitiated();
        ASSERT(reducetask->getExternalDepCount() == 0);
        runTask(reducetask, iteration);

        DOUT(g_task_dbg, "Rank-" << d_myworld->myrank() << " Running OPP task:");;

      }
      ASSERT(reducetask->getTask()->m_phase == currphase);

      numTasksDone++;
      if (g_task_order && d_myworld->myrank() == d_myworld->size() / 2) {
        DOUT(true, d_myworld->myrank() << " Running task static order: " << reducetask->getStaticOrder() << " , scheduled order: " << numTasksDone);
      }
      phaseTasksDone[reducetask->getTask()->m_phase]++;
    }

    if (numTasksDone < ntasks) {
      if (phaseTasks[currphase] == phaseTasksDone[currphase]) {
        currphase++;
      }
      else if (dts->numExternalReadyTasks() > 0 || dts->numInternalReadyTasks() > 0
               || (phaseSyncTask.find(currphase) != phaseSyncTask.end() && phaseTasksDone[currphase] == phaseTasks[currphase] - 1))  // if there is work to do
          {
        processMPIRecvs(TEST);  // receive what is ready and do not block
      }
      else {
        // we have nothing to do, so wait until we get something
        processMPIRecvs(WAIT_ONCE);  // there is no other work to do so block until some receives are completed
      }
    }

    if (!abort && m_dws[m_dws.size() - 1] && m_dws[m_dws.size() - 1]->timestepAborted()) {
      // TODO - abort might not work with external queue...
      abort = true;
      abort_point = task->getTask()->getSortedOrder();
      DOUT(g_dbg, "Aborting timestep after task: " << *task->getTask());
    }
  } // end while( numTasksDone < ntasks )

  if (g_queue_length) {
    float lengthsum = 0;
    totaltasks += ntasks;
    for (unsigned int i = 1; i < histogram.size(); i++) {
      lengthsum = lengthsum + i * histogram[i];
    }
    float queuelength = lengthsum / totaltasks;
    float allqueuelength = 0;
    Uintah::MPI::Reduce(&queuelength, &allqueuelength, 1, MPI_FLOAT, MPI_SUM, 0, d_myworld->getComm());
    proc0cout << "average queue length:" << allqueuelength / d_myworld->size() << std::endl;
  }
  
  if (g_timeout) {
    emitTime("MPI send time", mpi_info_[TotalSendMPI]);
    emitTime("MPI Testsome time", mpi_info_[TotalTestMPI]);
    emitTime("Total send time", mpi_info_[TotalSend] - mpi_info_[TotalSendMPI] - mpi_info_[TotalTestMPI]);
    emitTime("MPI recv time", mpi_info_[TotalRecvMPI]);
    emitTime("MPI wait time", mpi_info_[TotalWaitMPI]);
    emitTime("Total recv time", mpi_info_[TotalRecv] - mpi_info_[TotalRecvMPI] - mpi_info_[TotalWaitMPI]);
    emitTime("Total task time", mpi_info_[TotalTask]);
    emitTime("Total MPI reduce time", mpi_info_[TotalReduceMPI]);
    emitTime("Total reduction time", mpi_info_[TotalReduce] - mpi_info_[TotalReduceMPI]);
    emitTime("Total comm time", mpi_info_[TotalRecv] + mpi_info_[TotalSend] + mpi_info_[TotalReduce]);

    double time = Time::currentSeconds();
    double totalexec = time - m_last_time;
    m_last_time = time;

    emitTime("Other excution time", totalexec - mpi_info_[TotalSend] - mpi_info_[TotalRecv] - mpi_info_[TotalTask] - mpi_info_[TotalReduce]);
  }

  // compute the net timings
  if( m_shared_state != nullptr ) {  // subschedulers don't have a sharedState
    computeNetRunTimeStats(m_shared_state->d_runTimeStats);
  }


  //---------------------------------------------------------------------------
  // New way of managing single MPI requests - avoids MPI_Waitsome & MPI_Donesome - APH 07/20/16
  //---------------------------------------------------------------------------
  // wait on all pending requests
  auto ready_request = [](CommRequest const& r)->bool { return r.wait(); };
  while ( m_sends.size() != 0u ) {
    CommRequestPool::iterator comm_sends_iter;
    if ( (comm_sends_iter = m_sends.find_any(ready_request)) ) {
      m_sends.erase(comm_sends_iter);
    } else {
      // TODO - make this a sleep? APH 07/20/16
    }
  }
  //---------------------------------------------------------------------------

  ASSERT(m_sends.size() == 0u);
  ASSERT(m_recvs.size() == 0u);


  if (m_restartable && tgnum == static_cast<int>(m_task_graphs.size()) - 1) {
    // Copy the restart flag to all processors
    int myrestart = m_dws[m_dws.size() - 1]->timestepRestarted();
    int netrestart;

    Uintah::MPI::Allreduce(&myrestart, &netrestart, 1, MPI_INT, MPI_LOR, d_myworld->getComm());

    if (netrestart) {
      m_dws[m_dws.size() - 1]->restartTimestep();
      if (m_dws[0]) {
        m_dws[0]->setRestarted();
      }
    }
  }

  finalizeTimestep();
  
  if( ( g_exec_out || g_timeout ) && !m_parent_scheduler ) {  // only do on toplevel scheduler
    outputTimingStats("DynamicMPIScheduler");
  }

  DOUT(g_dbg, "Rank-" <<  me << " DynamicMPIScheduler finished executing");
}

