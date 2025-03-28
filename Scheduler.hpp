//
//  Scheduler.hpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#ifndef Scheduler_hpp
#define Scheduler_hpp

#include <vector>
#include <iostream>
#include <queue>

#include "Interfaces.h"

class Scheduler {
public:
    Scheduler()                 {}
    void Init();
    void MigrationComplete(Time_t time, VMId_t vm_id);
    void NewTask(Time_t now, TaskId_t task_id);
    void PeriodicCheck(Time_t now);
    void Shutdown(Time_t now);
    void TaskComplete(Time_t now, TaskId_t task_id);
private:
    unsigned get_id(MachineState_t state, CPUType_t cpu_type, bool gpu_enabled);
    unsigned get_virtual_id(VMType_t vm_state, SLAType_t state, CPUType_t cpu_type, bool gpu_enabled);
    void MonitorQueue(unsigned vm_id);
    vector<vector<MachineId_t>> machines;
    vector<vector<VMId_t>> vms;
    vector<queue<TaskId_t>> queues;
};



#endif /* Scheduler_hpp */
