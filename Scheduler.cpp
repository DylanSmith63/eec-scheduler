#include "Scheduler.hpp"

static bool migrating = false;
static unsigned active_machines;

unsigned Scheduler::get_id(MachineState_t state, CPUType_t cpu_type, bool gpu_enabled) {
    return (state << 3) + (cpu_type << 1) + gpu_enabled;
}

unsigned Scheduler::get_virtual_id(VMType_t vm_state, SLAType_t state, CPUType_t cpu_type, bool gpu_enabled) {
    return (vm_state << 5) + (state << 3) + (cpu_type << 1) + gpu_enabled;
}

void Scheduler::Init() {
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(Machine_GetTotal()), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);
    active_machines = Machine_GetTotal();
    vector<vector<MachineId_t>> grouped_machines(8);
    for (unsigned i = 0; i < active_machines; i++) {
        MachineInfo_t machine_info = Machine_GetInfo(MachineId_t(i));
        int group = (machine_info.cpu << 1) + machine_info.gpus;
        grouped_machines[group].push_back(MachineId_t(i));
    }
    machines = vector<vector<MachineId_t>>(32);
    for (int group = 0; group < 8; group++) {
        int num_machines = (int)grouped_machines[group].size();

        // ~40% (round up) of machines are initially in state S0
        int num_s0_machines = (num_machines * 4 + 9) / 10;
        num_machines -= num_s0_machines;

        // ~30% of machines are initially in state S0i1
        int num_s0i1_machines = (num_machines + 1) / 2;
        num_machines -= num_s0i1_machines;

        // ~20% of machines are initially in state S1
        int num_s1_machines = (num_machines * 2 + 2) / 3;
        num_machines -= num_s1_machines;

        // ~10% of machines are initially in state S2
        int num_s2_machines = num_machines;
        int cur = 0;
        for (int i = 0; i < num_s0_machines; i++)
            machines[(MachineState_t::S0 << 3) + group].push_back(grouped_machines[group][cur++]);
        for (int i = 0; i < num_s0i1_machines; i++)
            machines[(MachineState_t::S0i1 << 3) + group].push_back(grouped_machines[group][cur++]);
        for (int i = 0; i < num_s1_machines; i++)
            machines[(MachineState_t::S1 << 3) + group].push_back(grouped_machines[group][cur++]);
        for (int i = 0; i < num_s2_machines; i++)
            machines[(MachineState_t::S2 << 3) + group].push_back(grouped_machines[group][cur++]);
    }
    
    // Create VMs
    vms = vector<vector<VMId_t>>(128);
    for (unsigned i = 0; i < 32; i++) {
        unsigned cpu_type = (i >> 1) & 3;
        for (unsigned vm_type = 0; vm_type < 4; vm_type++) {
            if (vm_type == VMType_t::WIN && !(cpu_type == CPUType_t::ARM || cpu_type == CPUType_t::X86)) continue;
            if (vm_type == VMType_t::AIX && !(cpu_type == CPUType_t::POWER)) continue;
            unsigned virtual_id = (vm_type << 5) + i;
            for (unsigned machine_id : machines[i]) {
                VMId_t vm_id = VM_Create(VMType_t(vm_type), CPUType_t(cpu_type));
                VM_Attach(vm_id, machine_id);
                vms[virtual_id].push_back(vm_id);
            }
        }
    }

    // Initialize queues
    queues = vector<queue<TaskId_t>>(128);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {}

void Scheduler::MonitorQueue(unsigned id) {
    while (!queues[id].empty()) {
        unsigned task_id = queues[id].front();
        TaskInfo_t task_info = GetTaskInfo(task_id);
        unsigned target_vm;
        bool target_found = 0;
        for (unsigned vm_id : vms[id]) {
            VMInfo_t vm_info = VM_GetInfo(vm_id);
            MachineInfo_t machine_info = Machine_GetInfo(vm_info.machine_id);
            if (machine_info.memory_used + task_info.required_memory <= machine_info.memory_size) {
                if (machine_info.active_tasks < machine_info.num_cpus) {
                    target_found = 1;
                    target_vm = vm_id;
                    break;
                }
            }
        }
        if (!target_found) break;
        queues[id].pop();

        // add task with low priority by default
        VM_AddTask(target_vm, task_id, Priority_t::LOW_PRIORITY);
    }
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    /* Get the task parameters
     IsGPUCapable(task_id);
     GetMemory(task_id);
     RequiredVMType(task_id);
     RequiredSLA(task_id);
     RequiredCPUType(task_id);
    Decide to attach the task to an existing VM, 
         vm.AddTask(taskid, Priority_T priority); or
    Create a new VM, attach the VM to a machine
         VM vm(type of the VM)
         vm.Attach(machine_id);
         vm.AddTask(taskid, Priority_t priority) or
    Turn on a machine, create a new VM, attach it to the VM, then add the task
    
    Turn on a machine, migrate an existing VM from a loaded machine....
    
    Other possibilities as desired */

    TaskInfo_t info = GetTaskInfo(task_id);
    SLAType_t sla = info.required_sla;
    unsigned vm_id = get_virtual_id(info.required_vm, sla, info.required_cpu, info.gpu_capable);
    /* as long as a machine exists, a virtual machine with priority at least the priority of this tasks exists
       (due to monotonicity of the distribution of machine states) */
    while(vms[vm_id].empty()){
        if((vm_id & 1) == info.gpu_capable)

            // check if an equivalent virtual w/ or w/o gpus exists
            vm_id ^= 1;
        else{

            // check if a virtual machine with the next-highest priority exists
            sla = SLAType_t(sla - 1);
            vm_id = get_virtual_id(info.required_vm, sla, info.required_cpu, info.gpu_capable);
        }
    }

    // add new task to corresponding queue
    queues[vm_id].push(task_id);
    
    // try actively reducing this queue
    MonitorQueue(vm_id);
}

void Scheduler::PeriodicCheck(Time_t now) {    
    // try reducing all queues periodically
    for (unsigned vm_id = 0; vm_id < vms.size(); vm_id++) {
        MonitorQueue(vm_id);
    }
}

void Scheduler::Shutdown(Time_t time) {
    // shutdown VMs
    for(vector<VMId_t>& vm_arr: vms)
        for(VMId_t vm : vm_arr)
            VM_Shutdown(vm);
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 4);
}

// Public interface below

static Scheduler Scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    Scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id) {
    SimOutput("HandleNewTask(): Received new task " + to_string(task_id) + " at time " + to_string(time), 4);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id) {
    SimOutput("HandleTaskCompletion(): Task " + to_string(task_id) + " completed at time " + to_string(time), 4);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id) {
    // The simulator is alerting you that machine identified by machine_id is overcommitted
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    // The function is called on to alert you that migration is complete
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) + " was completed at time " + to_string(time), 4);
    Scheduler.MigrationComplete(time, vm_id);
    migrating = false;
}

void SchedulerCheck(Time_t time) {
    // This function is called periodically by the simulator, no specific event
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
}

void SimulationComplete(Time_t time) {
    // This function is called before the simulation terminates Add whatever you feel like.
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;     // SLA3 do not have SLA violation issues
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    TaskInfo_t task_info = GetTaskInfo(task_id);
    Priority_t priority = task_info.priority;
    if (priority != Priority_t::HIGH_PRIORITY) priority = Priority_t(priority - 1);
    SetTaskPriority(task_id, priority);
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {}

