// cd "/mnt/c/Users/erasa/OneDrive - nyu.edu/new york/New york university/fall -  sem 2/OS Franke/Labs/lab3"
// g++ mmu_v0.cpp -o mmu_v0
// ./mmu_v0 -f16 -ar -oOPFS in1 rfile
//make
// cd lab3_assign
// ./runit.sh outdir ../mmu_v0
// ./gradeit.sh . outdir

#include <iostream>
#include <stdio.h> 
#include <unistd.h> 
#include <fstream>
#include <string>
#include <deque>
#include <cstdint>
#include <iterator>
#include <vector>
#include <sstream>
using namespace std;

static const int MAX_VPAGES = 64;
static const int MAX_FRAMES = 128; //7 bits
static const int RESET_REFERENCE_PERIOD = 48;
static const int WORKING_SET_THRESHOLD = 50;
unsigned long instr_num = 0;
unsigned long process_exits = 0;
unsigned long context_switches = 0;
unsigned long long total_cost = 0;
vector<unsigned int> random_numbers;
int rand_index = 0;


struct VMA{
    unsigned long start_vpage;
    unsigned long end_vpage;
    bool write_protected;
    bool file_mapped;
};

// struct PageTableEntry{
//     int present=0; //present/valid - ‘1’ if the values in this entry is valid, otherwise translation is invalid and a pagefault exception will be raised
//     int referenced=0;// every time the page is accessed (load or store), the reference bit is set.
//     int modified=0;// every time the page is written to (store), the modified bit is set.
//     int write_protect=0; //1:page can only be read (load instruction). if attempted to write (store instruction) exception is raised. 
//     int swapped=0; //1: page has been swapped out to the disk.
//     // int kernel_protect=0; //1: only kernel can translate this page else privlege exception will be raised. 
//     int fileBacked=0; ///swapped or filebakced?????//
//     int frame_num;// this is the physical frame that is accessed based on the translation.
//     int reserved; 
// };

struct PageTableEntry {
    unsigned int present : 1;
    unsigned int referenced : 1;
    unsigned int modified : 1;
    unsigned int write_protect : 1;
    unsigned int swapped : 1;
    unsigned int fileBacked : 1;
    unsigned int frame_num : 7;  // supports up to 128 frames
    unsigned int reserved : 19;  // 32-13
};


struct FrameEntry{
    int frame_index;
    unsigned long page_index=-1;
    int process_id=-1;
    unsigned long age=0; //for aging algo
    int last_used_time=0; //for working set algo
};

vector<FrameEntry> frame_table(MAX_FRAMES);
deque<FrameEntry*> freeFrames;

struct Process{
    int pid;
    int num_vma;
    vector<VMA> vma_list;
    bool terminated = false;
    vector<PageTableEntry> page_table; 
    Process() : page_table(MAX_VPAGES) {}

    const VMA* get_vma_for_vpage(unsigned long vpage) const {
        for (const auto& vma : vma_list) {
            if (vpage >= vma.start_vpage && vpage <= vma.end_vpage) {
                return &vma;
            }
        }
        return nullptr; // No VMA found for this virtual page
    }

    //process stats
    unsigned long segv_faults = 0;
    unsigned long segprot_faults = 0;
    unsigned long unmaps = 0;
    unsigned long maps = 0;
    unsigned long ins = 0; //pageins
    unsigned long fins = 0; //pageins
    unsigned long outs = 0; //pageouts
    unsigned long fouts = 0; //pageouts
    unsigned long zeros = 0;
};

// vector<PageTableEntry> frame_table(MAX_FRAMES);

std::vector<Process> processes;
Process* current_process = nullptr;


struct Instruction{
    char spec;
    int value;
};
std::vector<Instruction> instructions;

struct InputConfig {
    int num_frames = 0;
    char algo;
    int num_processes = 0;
    std::string options;
    bool O = false;
    bool P = false;
    bool F = false;
    bool S = false;
    bool x = false;
    bool y = false;
    bool f = false;
    bool a = false;
    std::string inputfile;
    std::string randfile;
};

InputConfig config;

class Pager{
    public:
    virtual FrameEntry* select_victim_frame() = 0;
    virtual void freeframe(FrameEntry* frame){}
    virtual void frameAllocated(FrameEntry* frame){}
    static Pager* getPager();
    virtual ~Pager() {}
};

Pager* pager = nullptr;

class FIFO: public Pager{
    int next_victim = 0;
    public:
    FrameEntry* select_victim_frame() override {
        // FIFO implementation
        while(frame_table[next_victim].process_id == -1){
            next_victim = (next_victim + 1) % frame_table.size();
        }
        FrameEntry* victim = &frame_table[next_victim];
        next_victim = (next_victim + 1) % frame_table.size();
        return victim;
    }
};

class Random: public Pager{
    public:
    FrameEntry* select_victim_frame() override {
        // Random implementation
        if(random_numbers.empty()){
            cerr << "Random numbers not loaded\n";
            exit(1);
        }
        int victim_index = random_numbers[rand_index] % frame_table.size();
        rand_index = (rand_index + 1) % random_numbers.size();
        return &frame_table[victim_index];
    }
};

class Clock: public Pager{
    int hand=-1;
    public:
    FrameEntry* select_victim_frame() override {
        // Clock implementation
        size_t processed_frame = 0;
        hand = (hand + 1) % frame_table.size();
        while(processed_frame < 2*frame_table.size()){
            FrameEntry& frame = frame_table[hand];
            if(frame.process_id >=0){
                PageTableEntry& pte = processes[frame.process_id].page_table[frame.page_index];
                if(pte.referenced == 0){
                    // select this frame as victim
                    return &frame;
                }
                else{
                    pte.referenced = 0;
                }
            }
            hand = (hand + 1) % frame_table.size();
            processed_frame++;
        }
        return &frame_table[0];
    }
};

long unsigned reset_time=-1;
class NRU: public Pager{
    private:
    int start = 0;
    public:
    FrameEntry* select_victim_frame() override {
        // NRU implementation
        bool reset_reference_bits = (instr_num - reset_time >= RESET_REFERENCE_PERIOD);
        // start = (start + 1) % frame_table.size();
        vector<FrameEntry*> classes(4,nullptr);

        for(size_t i=0; i < frame_table.size(); i++){
            FrameEntry& frame = frame_table[(start+i) % frame_table.size()];
            if(frame.process_id < 0){
                continue;
            }
            else{
                PageTableEntry& pte = processes[frame.process_id].page_table[frame.page_index];
                int class_index = 2*pte.referenced + pte.modified;
                if(class_index >=0 && class_index < 4){ 
                    if(!classes[class_index]){
                        classes[class_index] = &frame;
                    }
                }
                if(reset_reference_bits){
                    pte.referenced = 0;
                }
            }
            // start = (start + 1) % frame_table.size();
        }
        FrameEntry* victim = nullptr;
        for(int i=0; i<4; i++){
            if(classes[i]){
                victim = classes[i];
                break;
            }
        }

        if(reset_reference_bits) reset_time = instr_num;
        start = (victim->frame_index+1)%frame_table.size();
        return victim;
        }
};

class Aging: public Pager{
    private:
    int start = 0;
    public:
    void frameAllocated(FrameEntry* frame) override {
        frame->age = 0;
    }
    FrameEntry* select_victim_frame() override {
        // Aging implementation
        uint32_t MIN_AGE = 0xFFFFFFFF;
        for(size_t i=0; i < frame_table.size(); i++){
            FrameEntry& frame = frame_table[start];
            if(frame.process_id >=0){
                PageTableEntry& pte = processes[frame.process_id].page_table[frame.page_index];
                frame.age = frame.age >> 1;
                if(pte.referenced){
                    frame.age = frame.age | 0x80000000; // set MSB
                }
                pte.referenced = 0;
            }
            start = (start + 1) % frame_table.size();
        }
        // FrameEntry* victim = &frame_table[start];
        size_t victim_index = start;

        for(size_t i=0; i < frame_table.size(); i++){
            FrameEntry& frame = frame_table[(i+start) % frame_table.size()];
            if(frame.process_id >=0){
                if(frame.age < MIN_AGE){
                    MIN_AGE = frame.age;
                    victim_index = (i+start) % frame_table.size();
                }
            }
        }
        start = (victim_index+1) % frame_table.size();
        return &frame_table[victim_index];
    }
};

class WorkingSet: public Pager{
    private:
    int start = 0;
    public:
    void frameAllocated(FrameEntry* frame) override {
        frame->age = 0;
    }
    FrameEntry* select_victim_frame() override {
        // Working Set implementation
        FrameEntry* oldest_frame = &frame_table[start];
        int oldest_used_frame_time = -1;
        for(size_t i=0; i < frame_table.size(); i++){
            FrameEntry& frame = frame_table[(i+start) % frame_table.size()];
            PageTableEntry& pte = processes[frame.process_id].page_table[frame.page_index];
            if(frame.process_id >=0){
                if(pte.referenced){
                    frame.last_used_time = instr_num;
                    pte.referenced = 0;
                }
                else{
                    int current_used_frame_time = instr_num - frame.last_used_time;
                    if(current_used_frame_time >= WORKING_SET_THRESHOLD){
                        start = (frame.frame_index+1) % frame_table.size();
                        return &frame;
                    }
                    if(current_used_frame_time > oldest_used_frame_time){
                        oldest_used_frame_time = current_used_frame_time;
                        oldest_frame = &frame;
                    }
                }
            }
        }
        start = (oldest_frame->frame_index+1) % frame_table.size();
        return oldest_frame;
    }
};

Pager* Pager::getPager(){
        switch (config.algo) {
            case 'f': return new FIFO();
            case 'r': return new Random();
            case 'c': return new Clock();
            case 'e': return new NRU();
            case 'a': return new Aging();
            case 'w': return new WorkingSet();
            default:
                cerr << "Invalid paging algo;\n";
                exit(1);
            }
    }

class Simulator{
    private:
    Pager* pager;
    FrameEntry* allocFrame(){;
        if(!freeFrames.empty()){
            FrameEntry* frame = freeFrames.front();
            freeFrames.pop_front();
            pager->frameAllocated(frame); // notify pager that frame is allocated
            return frame;
        }
        else{
            FrameEntry* victim = pager->select_victim_frame();
            Process& victim_process = processes[victim->process_id];
            PageTableEntry& pte = victim_process.page_table[victim->page_index];

            // unmap the page
            if (config.O) {
                cout << " UNMAP " << victim_process.pid << ":" << victim->page_index << endl;
            }
            victim_process.unmaps++;
            total_cost += 425;

            if(pte.modified){
                if(pte.fileBacked){
                    if (config.O) {
                        cout << " FOUT\n";
                    }
                    victim_process.fouts++;
                    total_cost += 2970;
                }
                else{
                    pte.swapped = 1;
                    if (config.O) {
                        cout << " OUT\n";
                    }
                    victim_process.outs++;
                    total_cost += 2740;
                    pte.swapped = 1;
                }
            }

            pte.present = 0;
            pte.referenced = 0;
            pte.modified = 0;

            pager->frameAllocated(victim); // notify pager that frame is freed and will be reallocated
            return victim;
        }
    }

    void PageFaultHandle(unsigned long vpage){
        const VMA* vma = current_process->get_vma_for_vpage(vpage);
        if(vma == nullptr){
            // segv fault
            if (config.O) {
                cout << " SEGV\n";
            }
            current_process->segv_faults++;
            total_cost += 514;
            return;
        }
        FrameEntry* frame = allocFrame();
        frame->process_id = current_process->pid;
        frame->page_index = vpage;

        PageTableEntry& pte = current_process->page_table[vpage];
        pte.present = 1;
        pte.referenced = 0;
        pte.write_protect = vma->write_protected;
        pte.fileBacked = vma->file_mapped;
        pte.modified = 0;
        pte.frame_num = frame->frame_index;

        if(vma->file_mapped){
            if (config.O) {
                cout << " FIN\n";
                current_process->fins++;
                total_cost += 3750;
            }
        }
        else if(pte.swapped){
            if (config.O) {
                cout << " IN\n";
                current_process->ins++;
                total_cost += 3610;
            }
        }
        else{
            if (config.O) {
                cout << " ZERO\n";
                current_process->zeros++;
                total_cost += 170;
            }
        }
        if (config.O) {
            cout << " MAP " << frame->frame_index << endl;
        }
        current_process->maps++;
        total_cost += 391;
    }

    void contextSwitch(int pid){
        total_cost += 150;
        context_switches++;
        current_process = &processes[pid];
    }
    void readvPage(unsigned long vpage){
        total_cost += 1;
        PageTableEntry& pte = current_process->page_table[vpage];
        if(pte.present==0){
            PageFaultHandle(vpage);
            if(pte.present==0){return;}
        }
        pte.referenced = 1;
    }

    void writevPage(unsigned long vpage){
        total_cost += 1;
        PageTableEntry& pte = current_process->page_table[vpage];
        if(pte.present==0){
            PageFaultHandle(vpage);
            if(pte.present==0){return;}
        }
        pte.referenced = 1;
        if(pte.write_protect){
            // segprot fault
            if (config.O) {
                cout << " SEGPROT\n";
            }
            current_process->segprot_faults++;
            total_cost += 529;
            return;
        }
        pte.modified = 1;
    }

    void exitProcess(int pid){
        total_cost += 3430;
        process_exits++;
        Process* proc = &processes[pid];
        if (config.O) {cout << "EXIT current process " << pid << '\n';}

        for(int i =0; i <MAX_VPAGES; i ++){
            PageTableEntry& pte = proc->page_table[i];
            if(pte.present==1){
                // unmap the page
                if (config.O) {
                    cout << " UNMAP " << pid << ":" << i << "\n";
                }
                proc->unmaps++;
                total_cost += 425;

                if(pte.modified ==1 && pte.fileBacked==1){
                    if (config.O) {
                        cout << " FOUT\n";
                    }
                    proc->fouts++;
                    total_cost += 2970;
                }

                FrameEntry& frame = frame_table[pte.frame_num];
                frame.page_index = -1;
                frame.process_id = -1;
                freeFrames.push_back(&frame);
                pager->freeframe(&frame);// notify pager that frame is freed
            }
            proc->page_table[i] = PageTableEntry(); // reset the page table entry
        }
        proc->terminated = true;
        if(current_process->pid == pid){
            current_process = nullptr;
        }
    }

    public:
    Simulator(Pager* p) : pager(p) {}
    void ProcessInstructions(){
        for(const auto& instr:instructions){
            char spec = instr.spec;
            unsigned long value = instr.value;
            // Process each instruction based on spec and value
            if (config.O) 
                {cout << instr_num << ": ==> " << spec << " " << value << "\n";}
            switch(spec){
                case 'c':
                    // context switch to process value
                    contextSwitch(value);
                    break;
                case 'r':
                    // read from virtual page value
                    readvPage(value);
                    break;
                case 'w':
                    // write to virtual page value
                    writevPage(value);
                    break;
                case 'e':
                    // exit process value
                    exitProcess(value);
                    break;
            }
            // if(config.x){}
            // if(config.y){}
            // if(config.f){}
            // if(config.a){}
            instr_num++;
        }
    }
};

class InputManager{
    public:
    void LoadInstructions(std::ifstream& file){
         auto nextline = [&](){
                std::string line;
                while(std::getline(file,line)){
                    if(line.empty() || line[0]=='#'){
                        continue;
                    }
                    return line;
                }
                return std::string{};
        };
        while(true){
            std::string line = nextline();
            if(line.empty()){
                break;
            }
            std::stringstream ss(line);
            char spec;
            int value;
            ss >> spec >> value;
            instructions.push_back({spec, value});
        }
    }

    bool LoadInputFile(const std::string& filename){
        try{
            // printf("Loading input file: %s\n", filename.c_str());
            std::ifstream file(filename);
            // file.open(filename); // redundant line file alrady opened in above line
            if(!file.is_open()){
                printf("-----line 541");
                return false;
            }
            auto nextline = [&](){
                std::string line;
                while(std::getline(file,line)){
                    if(line.empty() || line[0]=='#'){
                        continue;
                    }
                    return line;
                }
                return std::string{};
            };
            
            int num_proc;
            std::string line = nextline();
            std::stringstream ss(line);
            ss >> num_proc;
            config.num_processes = num_proc;
            for (int i=0;i<num_proc;i++){
                Process proc;
                proc.pid = i; // pid is just the index of the process in the input file
                int num_vma;
                std::string line = nextline();
                std::stringstream ss(line);
                ss >> num_vma;
                proc.num_vma = num_vma;
                for(int j=0;j<num_vma;j++){
                    VMA vma;
                    unsigned long svp, evp;
                    bool wp, fm;
                    std::string line = nextline();    
                    std::stringstream ss(line);
                    ss >> svp >> evp >> wp >> fm;
                    vma.start_vpage = svp;
                    vma.end_vpage = evp;    
                    vma.write_protected = wp;
                    vma.file_mapped = fm;
                    proc.vma_list.push_back(vma);
                }
                processes.push_back(proc);
            }
            LoadInstructions(file);
            file.close();
            return true;
        }
        catch(const std::exception& e){
            std::cerr <<"Error opening input file: " <<e.what()<<std::endl;
            return false;
        }
    }
    bool LoadRandomFile(const std::string& filename){
        ifstream file(filename);
        if(!file.is_open()){
                return false;
        }
        istream_iterator<unsigned int> start(file), end;
        ++start;
        random_numbers = vector<unsigned int>(start, end);
        return true;
    }       
};

// void debug(){
//     std::cout << "Debugging info" << std::endl;
//     std::cout << "Number of processes: " << config.num_processes << std::endl;
//     for(const auto& proc: processes){
//         std::cout << "Process ID: " << proc.pid << std::endl;
//         std::cout << "Number of VMAs: " << proc.num_vma << std::endl;
//         for(const auto& vma: proc.vma_list){
//             std::cout << "VMA Start: " << vma.start_vpage << ", End: " << vma.end_vpage 
//                       << ", Write Protected: " << vma.write_protected 
//                       << ", File Mapped: " << vma.file_mapped << std::endl;
//         }
//     }
//     for(const auto& instr: instructions){
//         std::cout << "Instruction: " << instr.spec << " " << instr.value <<std::endl;
//     } 
// }

void output_stats(){
    if(config.P){
        // output_page_tables();
        for(const auto& proc:processes){
            // if(proc)
            printf("PT[%d]:", proc.pid);
            for(int i=0; i< MAX_VPAGES; i++){
                printf(" ");
                const PageTableEntry& pte = proc.page_table[i];
                if(pte.present){
                    printf("%d:", i);
                    if(pte.referenced) printf("R");
                    else printf("-");
                    if(pte.modified) printf("M");
                    else printf("-");
                    if(pte.swapped) printf("S");
                    else printf("-");
                    // printf("%d", pte.frame_num);
                }
                else{
                    if(pte.swapped){
                        printf("#");
                    }else{
                        printf("*");
                    }
                }
            }
            printf("\n");
        }
    }
    if(config.F){
        // output_frame_table();
        printf("FT:");
        for(const auto& frame:frame_table){
            printf(" ");
            if(frame.process_id==-1){
                cout<<"*";
            }else{
                cout<<frame.process_id<<":"<<frame.page_index;
            }
        }
        printf("\n");
    }
    if(config.S){
        for(const auto& proc:processes){
            printf("PROC[%d]: U=%lu M=%lu I=%lu O=%lu FI=%lu FO=%lu Z=%lu SV=%lu SP=%lu\n", 
                     proc.pid, 
                     proc.unmaps, proc.maps, proc.ins, proc.outs, 
                     proc.fins, proc.fouts, proc.zeros, 
                     proc.segv_faults, proc.segprot_faults); 
        }
        printf("TOTALCOST %lu %lu %lu %llu %lu\n",  
           instr_num, context_switches, process_exits, total_cost, sizeof(PageTableEntry)); 
    }
}

int main(int argc, char* argv[]){
    // std::cerr << "Starting program..." << std::endl;

    //usage:  –f<num_frames> -a<algo>  [-o<options>] inputfile   randomfile (arguments can be in any order)

    int opt;
    
    // put ':' in the starting of the string so that program can distinguish between '?' and ':' ----------?????????????????
    // for (int i=0; i<argc; i++) {
    // cout << "arg[" << i << "] = " << argv[i] << endl;
    // }

    while((opt = getopt(argc, argv, ":f:a:o:")) != -1) 
    { 
        switch(opt) 
        { 
            case 'f': 
                config.num_frames = atoi(optarg);
                // printf("num_frames: %d\n", config.num_frames);
                frame_table.resize(config.num_frames);
                for(int i=0; i<config.num_frames; i++){
                    frame_table[i].frame_index = i;
                    freeFrames.push_back(&frame_table[i]);
                } 
                break;
            case 'a': 
                config.algo = optarg[0]; 
                // printf("algo: %c\n", config.algo);
                break;
            case 'o': 
                config.options = std::string(optarg); 
                // printf("options: %s\n", config.options.c_str());
                // config.O=config.options.contains('O');
                // config.P=config.options.contains('P');
                // config.F=config.options.contains('F');
                // config.S=config.options.contains('S');
                // config.x=config.options.contains('x');
                // config.y=config.options.contains('y');
                // config.f=config.options.contains('f');
                // config.a=config.options.contains('a');
                config.O = (config.options.find('O') != std::string::npos);
                config.P = (config.options.find('P') != std::string::npos);
                config.F = (config.options.find('F') != std::string::npos);
                config.S = (config.options.find('S') != std::string::npos);
                config.x = (config.options.find('x') != std::string::npos);
                config.y = (config.options.find('y') != std::string::npos);
                config.f = (config.options.find('f') != std::string::npos);
                config.a = (config.options.find('a') != std::string::npos);

                break;
            case ':':  
                // printf("option needs a value\n"); 
                break; 
            case '?': 
                printf("unknown option: %c\n", optopt);
                printf("Usage:  –f<num_frames> -a<algo>  [-o<options>] inputfile randomfile (arguments can be in any order)\n");
                break; 
        } 
    } 
    // optind points to the first non-option argument
    if (optind < argc) {
        config.inputfile = argv[optind++];  // first extra argument -> input file
    }

    if (optind < argc) {
        config.randfile = argv[optind++];   // second extra argument -> random file
    }

    // If there are still extra arguments, warn the user
    for (; optind < argc; optind++) {
        printf("Unknown extra argument: %s\n", argv[optind]);
    }

    
    // optind is for the extra arguments which are not parsed
    // for(; optind < argc; optind++){     
    //     if(std::string(argv[optind]).substr(0,2)=="in") config.inputfile=std::string(argv[optind]);
    //     else if(std::string(argv[optind])=="rfile") config.randfile=std::string(argv[optind]);
    // }
    // optind is for the extra arguments which are not parsed by getopt
    // for (; optind < argc; optind++) {
    //     std::string arg = argv[optind];

    //     // Remove possible "./" prefix
    //     if (arg.substr(0, 2) == "./") {
    //         arg = arg.substr(2);
    //     }

    //     if (arg == "in") {
    //         config.inputfile = argv[optind];   // keep original argv to preserve './' if needed
    //     } else if (arg == "rfile") {
    //         config.randfile = argv[optind];    // same here
    //     } else {
    //         printf("Unknown argument: %s\n", argv[optind]);
    //     }
    // }

    // printf("inputfile: %s\n", config.inputfile.c_str());
    // printf("randomfile: %s\n", config.randfile.c_str());

    pager = Pager::getPager();

    InputManager im;
    if(!im.LoadInputFile(config.inputfile))
    {
        cerr << "Failed to load input file: " << config.inputfile << endl;
        return 1;
    }
    if(config.algo=='r') {
    if(!im.LoadRandomFile(config.randfile))
        {
            cerr << "Failed to load random file: " << config.randfile << endl;
            return 1;
        }
    }

    Simulator sim(pager);
    sim.ProcessInstructions();

    output_stats();
    // debug();
    delete pager;
    return 0;
}



