/*
 * The MIT License
 *
 * Copyright (c) 1997-2017 The University of Utah
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

#ifndef UINTAH_HOMEBREW_SIMULATIONCONTROLLER_H
#define UINTAH_HOMEBREW_SIMULATIONCONTROLLER_H

#include <sci_defs/papi_defs.h> // for PAPI performance counters

#include <Core/Grid/GridP.h>
#include <Core/Grid/LevelP.h>
#include <Core/Grid/SimulationState.h>
#include <Core/Grid/SimulationStateP.h>
#include <Core/Parallel/UintahParallelComponent.h>
#include <Core/ProblemSpec/ProblemSpec.h>
#include <Core/ProblemSpec/ProblemSpecP.h>
#include <Core/Util/Timers/Timers.hpp>

#include <CCA/Ports/DataWarehouseP.h>
#include <CCA/Ports/Scheduler.h>
#include <CCA/Ports/SchedulerP.h>

#include <sci_defs/visit_defs.h>

#ifdef HAVE_VISIT
#  include <VisIt/libsim/visit_libsim.h>
#endif

// Window size for the overhead calculation
#define OVERHEAD_WINDOW 40

// Window size for the exponential moving average
#define AVERAGE_WINDOW 10

namespace Uintah {

class  DataArchive;
class  LoadBalancerPort;
class  Output;
class  Regridder;
class  SimulationInterface;
class  SimulationTime;

/**************************************

 CLASS
 WallTimer

 KEYWORDS
 Util, Wall Timers

 DESCRIPTION
 Utility class to manage the Wall Time.

 ****************************************/

class WallTimers {

public:
  WallTimers() { d_nSamples = 0; d_wallTimer.start(); };

public:

  Timers::Simple TimeStep;           // Total time for all time steps
  Timers::Simple ExpMovingAverage;   // Execution exponential moving average
                                     // for N time steps.
  Timers::Simple InSitu;             // In-situ time for previous time step

  int    getWindow( void ) { return AVERAGE_WINDOW; };
  void resetWindow( void ) { d_nSamples = 0; };
  
  Timers::nanoseconds updateExpMovingAverage( void )
  {
    Timers::nanoseconds laptime = TimeStep.lap();
    
    // Ignore the first sample as that is for initalization.
    if( d_nSamples )
    {
      // Calulate the exponential moving average for this time step.
      // Multiplier: (2 / (Time periods + 1) )
      // EMA: {current - EMA(previous)} x multiplier + EMA(previous).
      
      double mult = 2.0 / ((double) std::min(d_nSamples, AVERAGE_WINDOW) + 1.0);
      
      ExpMovingAverage = mult * laptime + (1.0-mult) * ExpMovingAverage();
    }
    else
      ExpMovingAverage = laptime;
      
    ++d_nSamples;

    return laptime;
  }

  double GetWallTime() { return d_wallTimer().seconds(); };

private:
  int d_nSamples;        // Number of samples for the moving average

  Timers::Simple d_wallTimer;
};


/**************************************
      
  CLASS
       SimulationController
      
       Short description...
      
  GENERAL INFORMATION
      
       SimulationController.h
      
       Steven G. Parker
       Department of Computer Science
       University of Utah
      
       Center for the Simulation of Accidental Fires and Explosions (C-SAFE)
       
             
  KEYWORDS
       Simulation_Controller
      
  DESCRIPTION
       Abstract baseclass for the SimulationControllers.
       Introduced to make the "old" SimulationController
       and the new AMRSimulationController interchangeable.
     
  WARNING
      
****************************************/

//! The main component that controls the execution of the 
//! entire simulation. 
class SimulationController : public UintahParallelComponent {

public:
  SimulationController( const ProcessorGroup* myworld, bool doAMR, ProblemSpecP pspec );
  virtual ~SimulationController();

  //! Notifies (before calling run) the SimulationController
  //! that this is simulation is a restart.
  void doRestart( const std::string& restartFromDir,
                  int           timestep,
                  bool          fromScratch,
                  bool          removeOldDir );

  //! Execute the simulation
  virtual void run() = 0;

  //  sets simulationController flags
  void setReduceUdaFlags( const std::string& fromDir );
     
  ProblemSpecP         getProblemSpecP() { return d_ups; }
  ProblemSpecP         getGridProblemSpecP() { return d_grid_ps; }
  SimulationStateP     getSimulationStateP() { return d_sharedState; }
  SchedulerP           getSchedulerP() { return d_scheduler; }
  LoadBalancerPort*    getLoadBalancer() { return d_lb; }
  Output*              getOutput() { return d_output; }
  SimulationTime*      getSimulationTime() { return d_timeinfo; }
  SimulationInterface* getSimulationInterface() { return d_sim; }
  Regridder*           getRegridder() { return d_regridder; }

  bool                 doAMR() { return d_doAMR; }

  WallTimers*          getWallTimers() { return &walltimers; }

protected:

  bool isLast( void );
  bool maybeLast( void );
    
  void restartArchiveSetup();
  void gridSetup();
  void regridderSetup();
  void simulationInterfaceSetup();
  void schedulerSetup();
  void loadBalancerSetup();
  void outputSetup();
  void timeStateSetup();
  void miscSetup();

  // Get the next delta T
  void getNextDeltaT( void );

  void ReportStats( bool first );     
  void getMemoryStats( bool create = false );
  void getPAPIStats  ( );
  
  ProblemSpecP         d_ups;
  ProblemSpecP         d_grid_ps;         // Problem Spec for the Grid
  ProblemSpecP         d_restart_ps;      // Problem Spec for restarting
  SimulationStateP     d_sharedState;
  SchedulerP           d_scheduler;
  LoadBalancerPort*    d_lb;
  Output*              d_output;
  SimulationTime*      d_timeinfo;
  SimulationInterface* d_sim;
  Regridder*           d_regridder;
  DataArchive*         d_restart_archive; // Only used when restarting: Data from UDA we are restarting from.

  GridP                d_currentGridP;

  bool d_doAMR;
  bool d_doMultiTaskgraphing;

  double d_delt;
  double d_prev_delt;
  
  double d_simTime;               // current sim time
  double d_startSimTime;          // starting sim time
  
  WallTimers walltimers;

  /* For restarting */
  bool        d_restarting;
  std::string d_fromDir;
  int         d_restartTimestep;
  int         d_restartIndex;
  int         d_lastRecompileTimestep;
  bool        d_reduceUda;
      
  // If d_restartFromScratch is true then don't copy or move any of
  // the old timesteps or dat files from the old directory.  Run as
  // as if it were running from scratch but with initial conditions
  // given by the restart checkpoint.
  bool d_restartFromScratch;

  // If !d_restartFromScratch, then this indicates whether to move
  // or copy the old timesteps.
  bool d_restartRemoveOldDir;

#ifdef USE_PAPI_COUNTERS
  int         d_eventSet;            // PAPI event set
  long long * d_eventValues;         // PAPI event set values

  struct PapiEvent {
    int         eventValueIndex;
    std::string name;
    std::string simStatName;
    bool        isSupported;

    PapiEvent( const std::string& _name, const std::string& _simStatName )
      : name(_name), simStatName(_simStatName)
    {
      eventValueIndex = 0;
      isSupported = false;
    }
  };

  std::map<int, PapiEvent>   d_papiEvents;
  std::map<int, std::string> d_papiErrorCodes;
#endif

#ifdef HAVE_VISIT
  bool CheckInSitu( visit_simulation_data *visitSimData, bool first );
#endif     

// Percent time in overhead samples
  double overheadValues[OVERHEAD_WINDOW];
  double overheadWeights[OVERHEAD_WINDOW];
  int    overheadIndex; // Next sample for writing

  int    d_nSamples;

  // void problemSetup( const ProblemSpecP&, GridP& ) = 0;
  // bool needRecompile( double t, double delt, const LevelP& level,
  //                     SimulationInterface* cfd, Output* output,
  //                     LoadBalancerPort* lb ) = 0;
  // SimulationController(const SimulationController&) = 0;
  // SimulationController& operator=(const SimulationController&) = 0;
};

} // End namespace Uintah

#endif
