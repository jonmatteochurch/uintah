#ifndef UT_BoundaryConditions_h
#define UT_BoundaryConditions_h

#include <CCA/Components/Arches/Task/TaskFactoryBase.h>
#include <string>

namespace Uintah{

  class BoundaryConditionFactory : public TaskFactoryBase {

  public:

    BoundaryConditionFactory( );
    ~BoundaryConditionFactory();

    void register_all_tasks( ProblemSpecP& db );

    void build_all_tasks( ProblemSpecP& db );

    std::vector<std::string> retrieve_task_subset( const std::string subset ) {


      throw InvalidValue("Error: Accessing the base class implementation of retrieve_task_subset in BoundaryConditionFactory, which means there is no specific implementation for this factory.",__FILE__,__LINE__);

    }

    void add_task( ProblemSpecP& db );

  protected:

  private:

  };
}
#endif
