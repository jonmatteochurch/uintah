/*
 * The MIT License
 *
 * Copyright (c) 1997-2018 The University of Utah
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

#include <CCA/Components/OnTheFlyAnalysis/meanTurbFluxes.h>
#include <CCA/Components/OnTheFlyAnalysis/FileInfoVar.h>

#include <CCA/Ports/ApplicationInterface.h>
#include <CCA/Ports/Scheduler.h>
#include <CCA/Ports/LoadBalancer.h>
#include <Core/Disclosure/TypeUtils.h>
#include <Core/Exceptions/ProblemSetupException.h>
#include <Core/Grid/DbgOutput.h>
#include <Core/Grid/Grid.h>
#include <Core/Grid/Material.h>
#include <Core/Grid/Variables/CellIterator.h>
#include <Core/Grid/Variables/PerPatch.h>
#include <Core/Math/MiscMath.h>
#include <Core/Parallel/ProcessorGroup.h>
#include <Core/Parallel/UintahParallelComponent.h>
#include <Core/Exceptions/InternalError.h>
#include <Core/OS/Dir.h> // for MKDIR
#include <Core/Util/FileUtils.h>
#include <Core/Util/DebugStream.h>
#include <Core/Util/DOUT.hpp>

#include <dirent.h>
#include <iostream>
#include <fstream>
#include <cstdio>

#define ALL_LEVELS 99
#define FINEST_LEVEL -1
using namespace Uintah;
using namespace std;
//__________________________________
//  To turn on the output
//  setenv SCI_DEBUG "meanTurbFluxes:+"
Dout dbg_OTF_MTF("meanTurbFluxes", "OnTheFlyAnalysis", "meanTurbFluxes debug stream", false);

//______________________________________________________________________
/*

______________________________________________________________________*/

meanTurbFluxes::meanTurbFluxes( const ProcessorGroup    * myworld,
                                const MaterialManagerP    materialManager,
                                const ProblemSpecP      & module_spec )
  : AnalysisModule(myworld, materialManager, module_spec)
{
  d_matl_set  = nullptr;
  d_zero_matl = nullptr;

  d_planeAve_1 = scinew planeAverage( myworld, materialManager, module_spec, true);
  d_planeAve_2 = scinew planeAverage( myworld, materialManager, module_spec, true);

  d_lb        = scinew meanTurbFluxesLabel();

  d_lb->lastCompTimeLabel =  VarLabel::create("lastCompTime_planeAve",
                                              max_vartype::getTypeDescription() );
  d_lb->fileVarsStructLabel = VarLabel::create("FileInfo_planeAve",
                                               PerPatch<FileInfoP>::getTypeDescription() );
  d_velVar =std::make_shared< velocityVar >();
}

//__________________________________
meanTurbFluxes::~meanTurbFluxes()
{
  DOUT(dbg_OTF_MTF, " Doing: destorying meanTurbFluxes" );
  if(d_matl_set && d_matl_set->removeReference()) {
    delete d_matl_set;
  }
   if(d_zero_matl && d_zero_matl->removeReference()) {
    delete d_zero_matl;
  }

  VarLabel::destroy(d_lb->lastCompTimeLabel);
  VarLabel::destroy(d_lb->fileVarsStructLabel);

  delete d_lb;
  delete d_planeAve_1;
  delete d_planeAve_2;
}

//______________________________________________________________________
//     P R O B L E M   S E T U P
void meanTurbFluxes::problemSetup(const ProblemSpecP &,
                                  const ProblemSpecP &,
                                  GridP & grid,
                                  std::vector<std::vector<const VarLabel* > > &PState,
                                  std::vector<std::vector<const VarLabel* > > &PState_preReloc)
{
  DOUT(dbg_OTF_MTF, "Doing problemSetup \t\t\t\t meanTurbFluxes" );

  d_planeAve_1->setComponents( m_application );
  d_planeAve_2->setComponents( m_application );

  const ProblemSpecP & notUsed = {nullptr};
  d_planeAve_1->problemSetup( notUsed, notUsed, grid, PState, PState_preReloc);
  d_planeAve_2->problemSetup( notUsed, notUsed, grid, PState, PState_preReloc);

  //__________________________________
  //  Read in timing information
  m_module_spec->require("samplingFrequency", d_writeFreq);
  m_module_spec->require("timeStart",         d_startTime);
  m_module_spec->require("timeStop",          d_stopTime);

  ProblemSpecP vars_ps = m_module_spec->findBlock("Variables");
  if (!vars_ps){
    throw ProblemSetupException("meanTurbFluxes: Couldn't find <Variables> tag", __FILE__, __LINE__);
  }

  //__________________________________
  // find the material to extract data from.  Default is matl 0.
  // The user can use either
  //  <material>   atmosphere </material>
  if(m_module_spec->findBlock("material") ){
    d_matl = m_materialManager->parseAndLookupMaterial(m_module_spec, "material");
  } else {
    d_matl = m_materialManager->getMaterial(0);
  }

  int defaultMatl = d_matl->getDWIndex();

  //__________________________________
  vector<int> m;
  m.push_back(0);            // matl for FileInfo label
  m.push_back(defaultMatl);

  // remove any duplicate entries
  sort(m.begin(), m.end());
  vector<int>::iterator it = unique(m.begin(), m.end());
  m.erase(it, m.end());

  //Construct the matl_set
  d_matl_set = scinew MaterialSet();
  d_matl_set->addAll(m);
  d_matl_set->addReference();

  // for fileInfo variable
  d_zero_matl = scinew MaterialSubset();
  d_zero_matl->add(0);
  d_zero_matl->addReference();


  //__________________________________
  //  Plane orientation
  string orient;
  m_module_spec->require("planeOrientation", orient);
  if ( orient == "XY" ){
    d_planeOrientation = XY;
  } else if ( orient == "XZ" ) {
    d_planeOrientation = XZ;
  } else if ( orient == "YZ" ) {
    d_planeOrientation = YZ;
  }

  //__________________________________
  //  velocity label
  map<string,string> attribute;
  ProblemSpecP vel_ps = m_module_spec->findBlock( "velocity" );

  if( vel_ps == nullptr ) {
    throw ProblemSetupException("meanTurbFluxes: velocity xml tag not found: ", __FILE__, __LINE__);
  }

  vel_ps->getAttributes( attribute );
  string labelName = attribute["label"];
  d_velVar->label   = VarLabel::find( labelName );

  if( d_velVar->label == nullptr ){
    throw ProblemSetupException("meanTurbFluxes: velocity label not found: " + labelName , __FILE__, __LINE__);
  }

  const TypeDescription * td     = CCVariable<Vector>::getTypeDescription();
  d_velVar->primeLabel            = VarLabel::create( labelName + "_prime",         td);
  d_velVar->normalTurbStrssLabel  = VarLabel::create( d_velVar->normalTurbStrssName, td);
  d_velVar->shearTurbStrssLabel   = VarLabel::create( d_velVar->shearTurbStrssName,  td);

  //__________________________________
  //  Now loop over all the variables to be analyzed

//  std::vector< std::shared_ptr< planarVarBase >  >planarVars;

  for( ProblemSpecP var_spec = vars_ps->findBlock( "analyze" ); var_spec != nullptr; var_spec = var_spec->findNextBlock( "analyze" ) ) {

    var_spec->getAttributes( attribute );

    //__________________________________
    // Read in the variable name
    string labelName = attribute["label"];
    VarLabel* label = VarLabel::find(labelName);
    if( label == nullptr ){
      throw ProblemSetupException("meanTurbFluxes: analyze label not found: " + labelName , __FILE__, __LINE__);
    }

    //__________________________________
    //  bulletproofing
    const TypeDescription* td = label->typeDescription();
    const TypeDescription* subtype = td->getSubType();

    const TypeDescription::Type baseType = td->getType();
    const TypeDescription::Type subType  = subtype->getType();

    // CC Variables, only Doubles and Vectors
    if(baseType != TypeDescription::CCVariable &&
       subType  != TypeDescription::double_type   ){
      ostringstream warn;
      warn << "ERROR:AnalysisModule:meanTurbFluxes: ("<<label->getName() << " "
           << " only CCVariable<double> variables work" << endl;
      throw ProblemSetupException(warn.str(), __FILE__, __LINE__);
    }

    //__________________________________
    //define intermdiate variable label names
    const TypeDescription * td_D   = CCVariable<double>::getTypeDescription();
    const TypeDescription * td_V   = CCVariable<Vector>::getTypeDescription();
    VarLabel* primeLabel     = VarLabel::create( labelName + "_prime",    td_D );        // Q'
    VarLabel* turbFluxLabel  = VarLabel::create( labelName + "_turbFlux", td_V );        // u'Q', v'Q', w'Q'

    // Bulletproofing - The user must specify the matl for single matl
    // variables
    if ( labelName == "press_CC" && attribute["matl"].empty() ){
      throw ProblemSetupException("meanTurbFluxes: You must add (matl='0') to the press_CC line." , __FILE__, __LINE__);
    }

    // Read in the optional level index
    int level = ALL_LEVELS;
    if (attribute["level"].empty() == false){
      level = atoi(attribute["level"].c_str());
    }

    //  Read in the optional material index
    int matl = defaultMatl;
    if (attribute["matl"].empty() == false){
      matl = atoi(attribute["matl"].c_str());
    }

    //__________________________________
    //  populate the vector of scalars
    Qvar* me = new Qvar();
    me->label         = label;
    me->primeLabel    = primeLabel;
    me->turbFluxLabel = turbFluxLabel;
    me->matl       = matl;
    me->level      = level;
    me->baseType   = baseType;
    me->subType    = subType;
    d_Qvars.push_back( std::shared_ptr<Qvar>(me) );
  }

//  d_allLevels_planarVars.at(0) = planarVars;
}

//______________________________________________________________________
void meanTurbFluxes::scheduleInitialize(SchedulerP   & sched,
                                      const LevelP & level)
{
  printSchedule(level,dbg_OTF_MTF,"meanTurbFluxes::scheduleInitialize");
//  planeAverage::scheduleInitialize( sched, level);
#if 0


  Task* t = scinew Task("meanTurbFluxes::initialize",
                  this, &meanTurbFluxes::initialize);

  t->setType( Task::OncePerProc );
  t->computes(d_lb->lastCompTimeLabel );
  t->computes(d_lb->fileVarsStructLabel, d_zero_matl);

  const PatchSet * perProcPatches = m_scheduler->getLoadBalancer()->getPerProcessorPatchSet(level);
  sched->addTask(t, perProcPatches,  d_matl_set);
#endif
}

//______________________________________________________________________
void meanTurbFluxes::initialize(const ProcessorGroup  *,
                                const PatchSubset     * patches,
                                const MaterialSubset  *,
                                DataWarehouse         *,
                                DataWarehouse         * new_dw)
{

  // With multiple levels a rank may not own any patches
  if(patches->size() == 0 ){
    return;
  }

  for(int p=0;p<patches->size();p++){
    const Patch* patch = patches->get(p);
    printTask( patch, dbg_OTF_MTF,"Doing meanTurbFluxes::initialize 1/2");

  }
}

//______________________________________________________________________
void meanTurbFluxes::scheduleRestartInitialize(SchedulerP   & sched,
                                               const LevelP & level)
{
  printSchedule(level,dbg_OTF_MTF,"meanTurbFluxes::scheduleRestartInitialize");

//  planeAverage::scheduleRestartInitialize( sched, level);

#if 0
  Task* t = scinew Task("meanTurbFluxes::initialize",
                  this, &meanTurbFluxes::initialize);


  t->setType( Task::OncePerProc );
  t->computes(d_lb->lastCompTimeLabel );
  t->computes(d_lb->fileVarsStructLabel, d_zero_matl);

  const PatchSet* perProcPatches = m_scheduler->getLoadBalancer()->getPerProcessorPatchSet(level);

  sched->addTask(t, perProcPatches,  d_matl_set);
#endif
}

//______________________________________________________________________
void
meanTurbFluxes::restartInitialize()
{
}
//______________________________________________________________________
void meanTurbFluxes::scheduleDoAnalysis(SchedulerP   & sched,
                                        const LevelP & level)
{
  printSchedule(level,dbg_OTF_MTF,"meanTurbFluxes::scheduleDoAnalysis");

  sched_TurbFluctuations( sched, level );
  
  sched_TurbFluxes(       sched, level );
  
//  planeAverage::scheduleDoAnalysis( sched, level );

}

//______________________________________________________________________
/*
    foreach y ( n_planes )
      iterate over all cells in Y plane {
        u' = u - u_bar(y)           Each plane in the grid will have a different _bar value
        v' = v - v_bar(y)           => CCVariable< Uintah:Vector >
        w' = w - w_bar(y)
      }
    end

    foreach y ( n_planes )
      foreach Q ( T, P, scalar )
        Q' = Q - Q_bar(y)          => CCVariable< double >
      end
    end
*/
//______________________________________________________________________
void meanTurbFluxes::sched_TurbFluctuations(SchedulerP   & sched,
                                            const LevelP & level)
{
  Task* t = scinew Task( "meanTurbFluxes::calc_TurbFluctuations",
                    this,&meanTurbFluxes::calc_TurbFluctuations );

  printSchedule(level,dbg_OTF_MTF,"meanTurbFluxes::sched_TurbFluctuations");

  t->requires( Task::OldDW, m_simulationTimeLabel );
  t->requires( Task::OldDW, d_lb->lastCompTimeLabel );

  // u,v,w -> u',v',w'
  t->requires( Task::NewDW, d_velVar->label, d_velVar->matSubSet, Ghost::None, 0 );
  t->computes ( d_velVar->primeLabel );
  
  // Q -> Q'
  for ( unsigned int i =0 ; i < d_Qvars.size(); i++ ) {
    std::shared_ptr< Qvar > Q = d_Qvars[i];
    t->requires( Task::NewDW, Q->label, Q->matSubSet, Ghost::None, 0 );
    t->computes ( Q->primeLabel );
  }
  sched->addTask( t, level->eachPatch() , d_matl_set );
}

//______________________________________________________________________
//
void meanTurbFluxes::calc_TurbFluctuations(const ProcessorGroup  * ,
                                           const PatchSubset    * patches,
                                           const MaterialSubset * ,
                                           DataWarehouse        * ,
                                           DataWarehouse        * new_dw)
{
  for( auto p=0;p<patches->size();p++ ){
    const Patch* patch = patches->get(p);
    printTask(patches, patch, dbg_OTF_MTF, "Doing meanTurbFluxes::calc_TurbFluctuations");

    // Q -> Q'
    for ( unsigned int i =0 ; i < d_Qvars.size(); i++ ) {
      std::shared_ptr< Qvar > Q = d_Qvars[i];
      calc_Q_prime< double >( new_dw, patch, Q );
    }

    // u,v,w -> u',v',w'
    calc_Q_prime< Vector >( new_dw, patch, d_velVar);
  }
}
//______________________________________________________________________
//
template <class T>
void meanTurbFluxes::calc_Q_prime( DataWarehouse         * new_dw,
                                   const Patch           * patch,
                                   std::shared_ptr<Qvar> Q)
{
  const int matl = Q->matl;

  constCCVariable<T> Qlocal;
  new_dw->get ( Qlocal, Q->label, matl, patch, Ghost::None, 0 );

  CCVariable< T > Qprime;
  new_dw->allocateAndPut( Qprime, Q->primeLabel, matl, patch );

  T Qbar = T(0);           // Need to add this!

  //__________________________________
  //
  for (CellIterator iter=patch->getCellIterator();!iter.done();iter++){
    IntVector c = *iter;
    Qprime[c] = Qlocal[c] - Qbar;

#if 0
    //__________________________________
    //  debugging
    if ( c == d_monitorCell ){
      cout << "  stats:  " << d_monitorCell <<  setw(10)<< Q.Label->getName()
           <<"\t Q_var: " << me
           <<"\t Qprime: "  << Qprime[c] << endl;
    }
#endif
  }
}


//______________________________________________________________________
/*
    iterate over all cells{
      u'u', v'v', w'w'      => CCVariable< Vector > mormalTurbStrss      
      u'v', v'w', w'u'      => CCVariable< Vector > shearTurbStrss

      // scalar
      foreach Q ( T, P, scalar )
        u'Q', v'Q', w'Q'    => CCVariable< Uintah:Vector > Q_turb_flux
      end
    }
*/
//______________________________________________________________________
void meanTurbFluxes::sched_TurbFluxes(SchedulerP   & sched,
                                      const LevelP & level)
{
  Task* t = scinew Task( "meanTurbFluxes::calc_TurbFluxes",
                    this,&meanTurbFluxes::calc_TurbFluxes );

  printSchedule(level,dbg_OTF_MTF,"meanTurbFluxes::sched_TurbFluxes");
  t->requires( Task::OldDW, m_simulationTimeLabel );
  t->requires( Task::OldDW, d_lb->lastCompTimeLabel );

  Ghost::GhostType gn  = Ghost::None;

  for ( unsigned int i =0 ; i < d_Qvars.size(); i++ ) {
    std::shared_ptr< Qvar > Q = d_Qvars[i];
    t->requires( Task::NewDW, Q->primeLabel, Q->matSubSet, gn, 0 );
    t->computes ( Q->turbFluxLabel );
  }

  t->requires( Task::NewDW, d_velVar->primeLabel, d_velVar->matSubSet, gn, 0 );
  t->computes ( d_velVar->normalTurbStrssLabel );
  t->computes ( d_velVar->shearTurbStrssLabel );

  sched->addTask( t, level->eachPatch() , d_matl_set );
}


//______________________________________________________________________
//
void meanTurbFluxes::calc_TurbFluxes(const ProcessorGroup * ,
                                     const PatchSubset    * patches,
                                     const MaterialSubset * ,
                                     DataWarehouse        * ,
                                     DataWarehouse        * new_dw)
{
  for( auto p=0;p<patches->size();p++ ){
    const Patch* patch = patches->get(p);
    printTask(patches, patch, dbg_OTF_MTF, "Doing meanTurbFluxes::calc_TurbFluxes");

    constCCVariable<Vector> velPrime;
    new_dw->get ( velPrime, d_velVar->primeLabel, d_velVar->matl, patch, Ghost::None, 0 );

    //__________________________________
    //  turbulent fluxes Q'u', Q'v', Q'w'
    for ( unsigned int i =0 ; i < d_Qvars.size(); i++ ) {
      std::shared_ptr< Qvar > Q = d_Qvars[i];

      const int matl = Q->matl;

      constCCVariable< double > Qprime;
      new_dw->get ( Qprime, Q->primeLabel, Q->matl, patch, Ghost::None, 0 );

      CCVariable< Vector > QturbFlux;
      new_dw->allocateAndPut( QturbFlux, Q->turbFluxLabel, matl, patch );

      //__________________________________
      //
      for (CellIterator iter=patch->getCellIterator();!iter.done();iter++){
        IntVector c = *iter;
        QturbFlux[c] = Qprime[c] * velPrime[c];
      }
    }    // QVars loop


    //__________________________________
    //   turbulent stresses
    CCVariable< Vector > diag;
    CCVariable< Vector > offdiag;

    new_dw->allocateAndPut( diag,    d_velVar->normalTurbStrssLabel, d_velVar->matl, patch );
    new_dw->allocateAndPut( offdiag, d_velVar->shearTurbStrssLabel,  d_velVar->matl, patch );


    for (CellIterator iter=patch->getCellIterator();!iter.done();iter++){
      IntVector c = *iter;
      Vector vel = velPrime[c];
      diag[c] = Vector( vel.x() * vel.x(),        // u'u'
                        vel.y() * vel.y(),        // v'v'
                        vel.z() * vel.z() );      // w'w'

      offdiag[c] = Vector( vel.x() * vel.y(),     // u'v'
                           vel.y() * vel.w(),     // v'w'
                           vel.z() * vel.z() );   // w'u'
    }
  }
}
