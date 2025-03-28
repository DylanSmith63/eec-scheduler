//
//  Scheduler.hpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#ifndef Scheduler_hpp
#define Scheduler_hpp

#include "Interfaces.h"
#include <queue>
#include <map>
#include <vector>
#include <iostream>
#include <utility>

class Scheduler {
public:
    Scheduler(){}
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
    map<TaskId_t, VMId_t> task_dict;
    map<MachineId_t, unsigned> tasks_in_machine;
    vector<vector<VMId_t>> vms;
    vector<priority_queue<pair<Time_t, TaskId_t>>> queues;
};



#endif /* Scheduler_hpp */