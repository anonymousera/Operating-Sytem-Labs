#include <iostream>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <queue>
#include <deque>
#include <list>
#include <string>
#include <cstring>
#include <cstdlib>
#include <climits>

enum ProcessState{
    STATE_CREATED,
    STATE_READY,
    STATE_RUNNING,
    STATE_BLOCKED,
    STATE_DONE
};

enum Transition_to_State{
    TRANS_TO_READY,
    TRANS_TO_RUN,
    TRANS_TO_BLOCK,
    TRANS_TO_PREEMPT,
    TRANS_TO_DONE
};

struct InputConfig {
    bool help = false;
    bool verbose = false;
    bool trace = false;
    bool event = false;
    bool preempt = false;
    std::string schedspec = "F";
    std::string schedulername;
    std::string inputfile;
    std::string randfile;
};

InputConfig config;

class Process{
    public:
    int pid;    
    int arrival_time;
    int total_cpu_time;
    int cpu_burst_max;
    int io_burst_max;
    int static_priority;
    int dynamic_priority;
    bool dynamic_prior_reset = false;

    ProcessState state;
    int process_start_time; // time when process started running for the first time
    int last_state_change_time; // timestamp of last state change
    int remaining_cpu_time;
    int curr_cpu_burst;
    int curr_io_burst;

    // output attributes
    int finish_time;
    int turnaround_time;
    int io_time; // time in blocked state
    int wait_time; // time in ready state


    Process(int p, int at, int tct, int cb, int ib, int sp)
        : pid(p), arrival_time(at), total_cpu_time(tct), cpu_burst_max(cb),
          io_burst_max(ib), static_priority(sp), dynamic_priority(sp - 1),
          state(STATE_CREATED), process_start_time(-1), last_state_change_time(0),
          remaining_cpu_time(tct), curr_cpu_burst(0), curr_io_burst(0),
          finish_time(0), turnaround_time(0), io_time(0), wait_time(0) {}
};

class Scheduler{
    public:
    int timequantum;
    int maxpriority;

    Scheduler(int quantum = 10000, int maxprio = 4) 
        : timequantum(quantum), maxpriority(maxprio) {}
    
    virtual ~Scheduler() {}
    
    virtual void add_process(Process* p) = 0;
    virtual Process* get_next_process() = 0;
    virtual bool unblock_preempt() { return false; }
    
    static Scheduler* getScheduler(const std::string& spec);
};

class FCFS_Scheduler : public Scheduler {
    public:
    FCFS_Scheduler() : Scheduler() {}
    std::deque<Process*> ready_queue;

    void add_process(Process* p) override {
        ready_queue.push_back(p);
    }
    Process* get_next_process() override { 
        if(!ready_queue.empty()) {
            Process* p = ready_queue.front();
            ready_queue.pop_front();
            return p;
        }
        return nullptr; 
    }
};

class LCFS_Scheduler : public Scheduler {
    public:
    LCFS_Scheduler() : Scheduler() {}
    std::deque<Process*> ready_queue;
    void add_process(Process* p) override {
        ready_queue.push_back(p);
    }
    Process* get_next_process() override { 
        if(!ready_queue.empty()) {
            Process* p = ready_queue.back();
            ready_queue.pop_back();
            return p;
        }
        return nullptr; 
    }
};

class SRTF_Scheduler : public Scheduler {
    public:
    SRTF_Scheduler() : Scheduler() {}
    std::list<Process*> ready_list;
    void add_process(Process* p) override {
        ready_list.push_back(p);
        ready_list.sort([](Process* a, Process* b) {return a->remaining_cpu_time < b->remaining_cpu_time;});
    }
    Process* get_next_process() override { 
        if(!ready_list.empty()) {
            Process* p = ready_list.front();
            ready_list.pop_front();
            return p;
        }
        return nullptr; 
    }
};

class RR_Scheduler : public Scheduler {
    public:
    RR_Scheduler(int quantum) : Scheduler(quantum) {}
    std::deque<Process*> ready_queue;
    void add_process(Process* p) override {
        ready_queue.push_back(p);
    }
    Process* get_next_process() override { 
        if(!ready_queue.empty()) {
            Process* p = ready_queue.front();
            ready_queue.pop_front();
            return p;
        }
        return nullptr; 
    }
};

class Prio_Scheduler : public Scheduler {
    private:
    std::vector<std::deque<Process*>> active_queues;
    std::vector<std::deque<Process*>> expired_queues;

    public:
    Prio_Scheduler(int quantum, int maxprio) : Scheduler(quantum, maxprio), active_queues(maxprio), expired_queues(maxprio) {}

    void add_process(Process* p) override {
        if(p->dynamic_prior_reset){
            expired_queues[p->dynamic_priority].push_back(p);
            p->dynamic_prior_reset = false;
        }
        else{
            active_queues[p->dynamic_priority].push_back(p);
        }
    }
    Process* get_next_process() override {
       for(int i=maxpriority-1;i>=0;i--){
            if(!active_queues[i].empty()){
                Process* p = active_queues[i].front();
                active_queues[i].pop_front();
                return p;
            }
        }
        bool active_empty = true;
        for(int i=0;i<maxpriority;i++){
            active_empty = active_empty && active_queues[i].empty();
        }
        if(active_empty){ //active queues are empty
            std::swap(active_queues, expired_queues);
            for(int i=maxpriority-1;i>=0;i--){
                    if(!active_queues[i].empty()){
                        Process* p = active_queues[i].front();
                        active_queues[i].pop_front();
                        return p;
                    }
            }
        }
        return nullptr;  
    }
};

class Preprio_Scheduler : public Scheduler {
    private:
    std::vector<std::deque<Process*>> active_queues;
    std::vector<std::deque<Process*>> expired_queues;

    public:
    Preprio_Scheduler(int quantum, int maxprio) : Scheduler(quantum, maxprio), active_queues(maxprio), expired_queues(maxprio) {}

    void add_process(Process* p) override {
        if(p->dynamic_prior_reset){
            expired_queues[p->dynamic_priority].push_back(p);
            p->dynamic_prior_reset = false;
        }
        else{
            active_queues[p->dynamic_priority].push_back(p);
        }
    }
    Process* get_next_process() override { 
        for(int i=maxpriority-1;i>=0;i--){
            if(!active_queues[i].empty()){
                Process* p = active_queues[i].front();
                active_queues[i].pop_front();
                return p;
            }
            }
        bool active_empty = true;
        for(int i=0;i<maxpriority;i++){
            active_empty = active_empty && active_queues[i].empty();
        }
        if(active_empty){//active queues are empty
            std::swap(active_queues, expired_queues);
            for(int i=maxpriority-1;i>=0;i--){
                    if(!active_queues[i].empty()){
                        Process* p = active_queues[i].front();
                        active_queues[i].pop_front();
                        return p;
                    }
            }
        }
        return nullptr; 
    }
    bool unblock_preempt() override { return true; }
};

class Event{
    public:
    int evtTimeStamp;
    Process* evtProcess;
    Transition_to_State transition;
    int evtID; // unique event ID for tie-breaking
    static int next_evtID;
    
    Event() {
        evtID = next_evtID++;
    }
    
    static bool priorityComparator(Event* a, Event* b) {
        if (a->evtTimeStamp != b->evtTimeStamp)
            return a->evtTimeStamp < b->evtTimeStamp;
        if(a->transition==TRANS_TO_DONE && b->transition!=TRANS_TO_DONE)
            return true;
        if(a->transition!=TRANS_TO_DONE && b->transition==TRANS_TO_DONE)
            return false;
        if(a->transition==TRANS_TO_PREEMPT && b->transition==TRANS_TO_BLOCK)
            return true;
        if(a->transition==TRANS_TO_BLOCK && b->transition==TRANS_TO_PREEMPT)
            return false;
        return a->evtID < b->evtID;
    }
};

int Event::next_evtID = 0;

class InputManager;

class DESSimulation{
    private:
    std::vector<Event*> event_queue; 
    std::vector<Process*> processes;
    Scheduler* scheduler;
    InputManager* input_mgr;  
    Process* current_running_process = nullptr;
    int current_time = 0;
    int io_active = 0;          // number of processes currently doing I/O
    int io_start_time = -1;     // when I/O became active last

    Event* get_next_event() {
        if (event_queue.empty()) return nullptr;
        Event* evt = event_queue.front();
        event_queue.erase(event_queue.begin());
        return evt;
    }
    
    void add_event(Event* evt) {
        auto it = event_queue.begin();
        while (it != event_queue.end() && Event::priorityComparator(*it, evt))
            ++it;
        event_queue.insert(it, evt);
    }
    
    int get_next_event_time() {
        if (event_queue.empty()) return -1;
        return event_queue.front()->evtTimeStamp;
    }
    bool haseventattime(Process* p, int time) {
        for (Event* evt : event_queue) {
            if (evt->evtProcess == p && evt->evtTimeStamp == time) {
                return true;
            }
        }
        return false;
    }

    void remove_event(Process* p) {
        auto it = event_queue.begin();
        while (it != event_queue.end()) {
            if ((*it)->evtProcess == p) {
                delete *it;
                it = event_queue.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    int myrandom(int burst);  
    
    public:
    int cpu_utilization;
    int io_utilization;

    DESSimulation(Scheduler* sched, InputManager* im) 
        : scheduler(sched), input_mgr(im), cpu_utilization(0),io_utilization(0) {}

    ~DESSimulation() {
        for (Event* evt : event_queue)
            delete evt;
        event_queue.clear();

        for (Process* p : processes)
            delete p;
        processes.clear();
    }


    void buildprocess(int pid, int at, int tct, int cb, int ib) {
        Process* proc = new Process(pid, at, tct, cb, ib, myrandom(scheduler->maxpriority));
        processes.push_back(proc);
        Event* evt = new Event();
        evt->evtProcess = proc;
        evt->evtTimeStamp = at;
        evt->transition = TRANS_TO_READY;
        add_event(evt);
    }    
    
    void Simulation() {
        Event* evt;
        while ((evt = get_next_event())) {
            
            Process* proc = evt->evtProcess;
            current_time = evt->evtTimeStamp;
            Transition_to_State transition = evt->transition; 
            int timeInPrevState = current_time - proc->last_state_change_time;
            bool CALL_SCHEDULER = false;

            switch (transition) {
                case TRANS_TO_READY: { 
                    // std::cerr << "Process " << proc->pid << " TRANS_TO_READY at time " << current_time << std::endl;

                    if(proc->state==STATE_CREATED){
                        proc->state = STATE_READY;
                        proc->last_state_change_time = current_time;
                        proc->dynamic_priority = proc->static_priority - 1;
                    }
                    else if(proc->state==STATE_BLOCKED){
                        io_active--;
                        if(io_active==0){
                            io_utilization+=current_time - io_start_time;
                            io_start_time=-1;
                        }
                        proc->state = STATE_READY;
                        proc->io_time += timeInPrevState;
                        proc->last_state_change_time = current_time;
                        proc->dynamic_priority = proc->static_priority - 1;
                    }
                    scheduler->add_process(proc);
                    // Preempt if needed
                    if (scheduler->unblock_preempt() && current_running_process != nullptr) {
                        if (proc->dynamic_priority > current_running_process->dynamic_priority && !haseventattime(current_running_process, current_time)) {                             
                            remove_event(current_running_process);
                            Event* preempt_evt = new Event();
                            preempt_evt->evtProcess = current_running_process;
                            preempt_evt->evtTimeStamp = current_time;
                            preempt_evt->transition = TRANS_TO_PREEMPT;
                            add_event(preempt_evt);
                        }
                    }
                    CALL_SCHEDULER = true;  
                    break;
                }

                case TRANS_TO_RUN: { 
                    // std::cerr << "Process " << proc->pid << " TRANS_TO_RUN at time " << current_time << std::endl;  
                    if(proc->curr_cpu_burst > 0){
                        proc->state = STATE_RUNNING;
                        proc->wait_time += timeInPrevState;
                        proc->last_state_change_time = current_time;
                    }
                    else if(proc->curr_cpu_burst == 0){
                        proc->state = STATE_RUNNING;
                        proc->curr_cpu_burst = std::min(proc->remaining_cpu_time, myrandom(proc->cpu_burst_max));
                        proc->wait_time += timeInPrevState;
                        proc->last_state_change_time = current_time;
                    }

                    current_running_process = proc;
                    Event* new_evt = new Event();
                    new_evt->evtProcess = proc;
                        
                    if (proc->curr_cpu_burst <= scheduler->timequantum) {
                        new_evt->evtTimeStamp = current_time + proc->curr_cpu_burst;
                        if (proc->remaining_cpu_time == proc->curr_cpu_burst) {
                                new_evt->transition = TRANS_TO_DONE;
                        } 
                        else {
                            // block process
                            new_evt->transition = TRANS_TO_BLOCK;
                        }
                    }
                    else {
                            // preempt process
                            new_evt->evtTimeStamp = current_time + scheduler->timequantum;
                            new_evt->transition = TRANS_TO_PREEMPT;
                        }
                    add_event(new_evt);
                    break;
                }

                case TRANS_TO_PREEMPT:{
                    // std::cerr << "Process " << proc->pid << " TRANS_TO_PREEMPT at time " << current_time << std::endl;
                    cpu_utilization+=timeInPrevState;
                    if(proc->state==STATE_RUNNING){
                        proc->state = STATE_READY;
                        proc->remaining_cpu_time -= timeInPrevState;
                        proc->curr_cpu_burst -= timeInPrevState;
                        proc->dynamic_priority--;
                        if (proc->dynamic_priority < 0 || config.schedspec[0] == 'R') {
                            proc->dynamic_priority = proc->static_priority - 1;
                            proc->dynamic_prior_reset=true;
                        }
                        proc->last_state_change_time = current_time;
                    }
                    scheduler->add_process(proc);
                    current_running_process = nullptr;
                    CALL_SCHEDULER = true; 
                    break;
                }
                    
                case TRANS_TO_BLOCK: { 
                    // std::cerr << "Process " << proc->pid << " TRANS_TO_BLOCK at time " << current_time << std::endl;

                    cpu_utilization+=timeInPrevState;
                    io_active++;
                    if(io_active==1){
                        io_start_time=current_time;
                    }
                    if(proc->state==STATE_RUNNING){
                        proc->state = STATE_BLOCKED;
                        proc->remaining_cpu_time -= timeInPrevState;
                        proc->curr_cpu_burst = 0;
                        proc->curr_io_burst = myrandom(proc->io_burst_max);
                        proc->last_state_change_time = current_time;
                        
                        Event* io_evt = new Event();
                        io_evt->evtProcess = proc;
                        io_evt->evtTimeStamp = current_time + proc->curr_io_burst;
                        io_evt->transition = TRANS_TO_READY;
                        add_event(io_evt);

                    }
                    current_running_process = nullptr;
                    CALL_SCHEDULER = true; 
                    break;
                }
                case TRANS_TO_DONE: {
                    // std::cerr << "Process " << proc->pid << " TRANS_TO_DONE at time " << current_time << std::endl;
                    cpu_utilization+=timeInPrevState;
                    proc->state = STATE_DONE;
                    proc->finish_time = current_time;
                    proc->turnaround_time = proc->finish_time - proc->arrival_time;
                    current_running_process = nullptr;
                    CALL_SCHEDULER = true;
                    break;
                }
            }
            
            delete evt; 
            evt = nullptr;
            
            if(CALL_SCHEDULER) {
                // std::cerr << "CALL_SCHEDULER at time " << current_time << std::endl;
                // std::cerr << "Next event time: " << get_next_event_time() << std::endl;
                // std::cerr << "current_running_process: " << current_running_process << std::endl; 

                if (get_next_event_time() == current_time)
                    continue;
                CALL_SCHEDULER = false;
                if (current_running_process == nullptr) { 
                    current_running_process = scheduler->get_next_process(); 
                    if (current_running_process != nullptr){
                        Event* new_evt = new Event();
                        new_evt->evtProcess = current_running_process;
                        new_evt->evtTimeStamp = current_time;
                        new_evt->transition = TRANS_TO_RUN;
                        add_event(new_evt);
                    }  
                }
            }
        }
    }
    
    void Summary() {
        printf("%s\n", config.schedulername.c_str());  
        int num_process = processes.size();
        for(int i=0;i<num_process;i++){
            Process* p = processes[i];
            printf("%04d: %4d %4d %4d %4d %1d | %5d %5d %5d %5d\n",  
                p->pid,
                p->arrival_time,
                p->total_cpu_time,
                p->cpu_burst_max,
                p->io_burst_max,
                p->static_priority,  
                p->finish_time,
                p->turnaround_time,
                p->io_time,
                p->wait_time);
        }

        int last_finish_time = current_time;

        double cpu_util_percent = (double)cpu_utilization * 100.0 / (double)last_finish_time;
        double io_util_percent = (double)io_utilization * 100.0 / (double)last_finish_time;

        double turnaround_avg = 0.0;
        double waittime_avg = 0.0;
        for(int i=0;i<num_process;i++){
            Process* p = processes[i];
            turnaround_avg += p->turnaround_time;
            waittime_avg += p->wait_time;
        }
        turnaround_avg /= num_process;
        waittime_avg /= num_process;

        double throughput = (double)num_process * 100.0 / (double)last_finish_time;
        printf("SUM: %d %.2lf %.2lf %.2lf %.2lf %.3lf\n", 
            last_finish_time,
            cpu_util_percent,
            io_util_percent,
            turnaround_avg,
            waittime_avg,
            throughput);

        return;
    }
};

Scheduler* Scheduler::getScheduler(const std::string& spec) {
    if(spec[0]=='F'){
        config.schedulername="FCFS";
        return new FCFS_Scheduler();
    }
    if(spec[0]=='L'){
        config.schedulername="LCFS";
        return new LCFS_Scheduler();
    }
    if(spec[0]=='S'){
        config.schedulername="SRTF";
        return new SRTF_Scheduler();
    }
    if(spec[0]=='R'){
        int quantum = std::stoi(spec.substr(1));
        config.schedulername="RR "+std::to_string(quantum);
        return new RR_Scheduler(quantum);
    }
    if (spec[0] == 'P') {
        int quantum = 0;
        int maxprio = 4;
        // sscanf(spec.c_str() + 1, "%d:%d", &quantum, &maxprio);
        sscanf(spec.c_str() + 1, "%d:%d", &quantum, &maxprio);
        config.schedulername="PRIO "+std::to_string(quantum);
        return new Prio_Scheduler(quantum, maxprio);
    }
    if (spec[0] == 'E') {
        int quantum = 0;
        int maxprio = 4;
        sscanf(spec.c_str() + 1, "%d:%d", &quantum, &maxprio);
        // printf("spec: '%s', ret=%d, quantum=%d, maxprio=%d\n", spec.c_str(), ret, quantum, maxprio);

        config.schedulername="PREPRIO "+std::to_string(quantum);
        return new Preprio_Scheduler(quantum, maxprio);
    }
    std::cerr << "Error: Unknown scheduling specification: " << spec << std::endl;
    return nullptr;
}

class InputManager{
private:
    std::vector<int> values;
    int offset;
    
public:
    InputManager(): offset(0) {}
    
    bool LoadRandomFile(const std::string& randfile) {
        try{ 
            std::ifstream input_randfile;
            input_randfile.open(randfile);
            if (!input_randfile.is_open()) {
                return false;
            }
            int count;
            input_randfile >> count;
            for (int i = 0; i < count; i++) {
                int value;
                if (!(input_randfile >> value)) {
                    std::cerr << "Error: Random file ended early." << std::endl;
                    return false;
                }
                values.push_back(value);
            }
            input_randfile.close();
            return true;
        }
        catch(const std::exception& e) {
            std::cerr << "Error loading random numbers: " << e.what() << std::endl;
            return false;
        }
    }

    int myrandom(int burst) {
        if (values.empty()) {
            std::cerr << "Error: Random values not loaded." << std::endl;
            return 0;
        }
        int value = 1 + (values[offset] % burst);
        offset = (offset + 1) % values.size();
        return value;
    }

    bool LoadInputFile(const std::string& inputfile, DESSimulation& sim) {
        try {
            std::ifstream input_file;
            input_file.open(inputfile);
            if (!input_file.is_open()) {
                return false;
            }
            int pid = 0;
            while (!input_file.eof()) {
                int at, tct, cb, ib;
                input_file >> at >> tct >> cb >> ib;
                if (input_file.fail()) {
                    break;
                }
                sim.buildprocess(pid, at, tct, cb, ib);
                pid++;
            }
            input_file.close();
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "Error loading input file: " << e.what() << std::endl;
            return false;
        }
    }
};

int DESSimulation::myrandom(int burst) {
    return input_mgr->myrandom(burst);
}

bool parseInput(int argc, char* argv[]){
    if(argc < 3) {
        return false;
    }
    int index=1;
    while(index < argc-2) {
        std::string arg = argv[index];
        if(arg == "-h") {
            config.help = true;
            index++;
        }
        else if(arg == "-v") {
            config.verbose = true;
            index++;
        }
        else if(arg == "-t") {
            config.trace = true;
            index++;
        }
        else if(arg == "-e") {
            config.event = true;
            index++;
        }
        else if(arg == "-p") {
            config.preempt = true;
            index++;
        }
        else if(arg.rfind("-s", 0) == 0) {
            if(arg.length() < 3) {
                std::cerr << "Error: -s requires a scheduling specification." << std::endl;
                return false;
            }
            config.schedspec = arg.substr(2);
            index++;
        }
        else {
            std::cerr << "Error: Unknown argument: " << arg << std::endl;
            return false;
        }
    }

    if(index != argc-2) {
        std::cerr << "Error: Missing input or rand file." << std::endl;
        return false;
    }
    config.inputfile = argv[argc-2];
    config.randfile = argv[argc-1];
    return true;
}

int main(int argc, char* argv[]) {
    // std::cerr << "Starting program" << std::endl;

    if (!parseInput(argc, argv)) {
        std::cerr << "Usage: " << argv[0] << " [-h] [-v] [-t] [-e] [-p] [-s<schedspec>] inputfile randfile\n";
        return 1;
    }
    // std::cerr << "Parsed input successfully" << std::endl;

    InputManager im;
    if(!im.LoadRandomFile(config.randfile)) {
        std::cerr << "Error loading random file: " << config.randfile << std::endl;
        return 1;
    }
    // std::cerr << "Loaded random file" << std::endl;

    Scheduler* scheduler = Scheduler::getScheduler(config.schedspec);
    if (!scheduler) {
        std::cerr << "Error: Failed to create scheduler" << std::endl;
        return 1;
    }

    // std::cerr << "Created scheduler: " << config.schedulername << std::endl;

    DESSimulation sim(scheduler, &im);

    // std::cerr << "Created simulation" << std::endl;

    if (!im.LoadInputFile(config.inputfile, sim)) {
        std::cerr << "Error loading input file: " << config.inputfile << std::endl;
        delete scheduler;
        return 1;
    }

    // std::cerr << "Loaded input file" << std::endl;
        
    sim.Simulation();

    // std::cerr << "Simulation complete" << std::endl;
    
    sim.Summary();

    // std::cerr << "Done" << std::endl;
    
    delete scheduler;
    return 0;
}
