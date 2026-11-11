// cd "/mnt/c/Users/erasa/OneDrive - nyu.edu/new york/New york university/fall -  sem 2/OS Franke/Labs/lab4"
// g++ -o iosched iosched.cpp
// ./iosched -sF testinput.txt

#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <map>
using namespace std;

int total_time = 0;
int tot_movement = 0;
double io_utilization = 0.0;
double avg_turnaround = 0.0;
double avg_waittime = 0.0;
int max_waittime = 0;

struct InputConfig{
    string inputfile;
    char algo = 'N'; // Default to FIFO
};
InputConfig config;

struct IORequest{
    int request_id;
    int issue_time;
    int track_num;
    int start_time = -1;
    int end_time = -1;
    IORequest() = default;

    IORequest(int id, int issue, int track)
        : request_id(id), issue_time(issue), track_num(track),
          start_time(-1), end_time(-1) {}
};

vector<IORequest> io_requests;

class IOScheduler{
    public:
    virtual void addRequest(IORequest req)=0;
    virtual IORequest getNextRequest(int current_track)=0;
    static IOScheduler* getScheduler(char algo);
};

class FIFOScheduler : public IOScheduler{
    private:
    queue<IORequest> request_queue;
    public:
    void addRequest(IORequest req) override{
        request_queue.push(req);
        // printf("Added Request ID %d to FIFO queue.\n", req.request_id);
    }
    IORequest getNextRequest(int current_track) override{
        if(request_queue.empty()){
            // throw runtime_error("No requests in queue");
            // cerr << "No requests in queue" << endl;
            return IORequest{-1,0,0}; // Return a dummy request
        }
        IORequest req = request_queue.front();
        request_queue.pop();
        return req;
    }
};

class SSTFScheduler : public IOScheduler{
    private:
    vector<IORequest> request_list;
    public:
    void addRequest(IORequest req) override{
        request_list.push_back(req);
    }
    IORequest getNextRequest(int current_track) override{
        if(request_list.empty()){
            // cerr << "No requests in list" << endl;
            return IORequest{-1,0,0}; // Return a dummy request
        }
        auto it = min_element(request_list.begin(), request_list.end(),
            [current_track](const IORequest& a, const IORequest& b){
                return abs(a.track_num - current_track) < abs(b.track_num - current_track);
            });
        IORequest req = *it;
        request_list.erase(it);
        return req;
    }
};

class LOOKScheduler : public IOScheduler{
    private:
    vector<IORequest> request_list;
    int direction=1;
    public:
    void addRequest(IORequest req) override{
            request_list.push_back(req);
    }
    IORequest getNextRequest(int current_track) override{
        if(request_list.empty()){
            // cerr << "No requests in list" << endl;
            return IORequest{-1,0,0}; // Return a dummy request
        }
        // Find requests in the current direction
        vector<IORequest> candidates;
        for(const auto& req : request_list){
            if((direction == 1 && req.track_num >= current_track) ||
               (direction == -1 && req.track_num <= current_track)){
                candidates.push_back(req);
            }
        }
        if(candidates.empty()){
            // Reverse direction if no candidates found
            direction = -direction;
            for(const auto& req : request_list){
                if((direction == 1 && req.track_num >= current_track) ||
                   (direction == -1 && req.track_num <= current_track)){
                    candidates.push_back(req);
                }
            }
        }
        auto it = min_element(candidates.begin(), candidates.end(),
            [current_track](const IORequest& a, const IORequest& b){
                return abs(a.track_num - current_track) < abs(b.track_num - current_track);
            });
        IORequest req = *it;
        // Remove from original list
        request_list.erase(remove_if(request_list.begin(), request_list.end(),
            [req](const IORequest& r){ return r.request_id == req.request_id; }), request_list.end());
        return req;
    }
};

// class CLOOKScheduler : public IOScheduler{
//     private:
//     vector<IORequest> request_list;
//     int direction=1;
//     public:
//     void addRequest(IORequest req) override{
//             request_list.push_back(req);
//     }
//     IORequest getNextRequest(int current_track) override{
//         if(request_list.empty()){
//             // cerr << "No requests in list" << endl;
//             return IORequest{-1,0,0}; // Return a dummy request
//         }
//         // Find requests in the current direction
//         vector<IORequest> candidates;
//         for(const auto& req : request_list){
//             if(req.track_num >= current_track){
//                 candidates.push_back(req);
//             }
//         }
//         if(candidates.empty()){
//             // Wrap around to the beginning
//             candidates = request_list;
//         }
//         auto it = min_element(candidates.begin(), candidates.end(),
//             [current_track](const IORequest& a, const IORequest& b){
//                 return abs(a.track_num - current_track) < abs(b.track_num - current_track);
//             });
//         IORequest req = *it;
//         // Remove from original list
//         request_list.erase(remove_if(request_list.begin(), request_list.end(),
//             [req](const IORequest& r){ return r.request_id == req.request_id; }), request_list.end());
//         return req;
//     }
// };

class CLOOKScheduler : public IOScheduler {
    private:
    vector<IORequest> request_list;
    
    public:
    void addRequest(IORequest req) override {
        request_list.push_back(req);
    }

    IORequest getNextRequest(int current_track) override {
        if (request_list.empty()) {
            return IORequest{-1,0,0};
        }

        IORequest* selected = nullptr;

        // find the smallest track >= current_track
        for (auto& req : request_list) {
            if (req.track_num >= current_track) {
                if (!selected || req.track_num < selected->track_num)
                    selected = &req;
            }
        }

        // if none exist, wrap around: pick smallest track overall
        if (!selected) {
            for (auto& req : request_list) {
                if (!selected || req.track_num < selected->track_num)
                    selected = &req;
            }
        }

        IORequest result = *selected;

        request_list.erase(
            remove_if(request_list.begin(), request_list.end(),
                [&](const IORequest& r){ return r.request_id == result.request_id; }),
            request_list.end()
        );

        return result;
    }
};

class FLOOKScheduler : public IOScheduler{
    private:
    vector<IORequest> active_queue;
    vector<IORequest> add_queue;
    int direction=1;
    public:
    void addRequest(IORequest req) override{
        // Implement FLOOK add request logic
        add_queue.push_back(req);
    }
    IORequest getNextRequest(int current_track) override{
        if(active_queue.empty()){
            if(add_queue.empty()){
                // cerr << "No requests in list" << endl;
                return IORequest{-1,0,0}; // Return a dummy request
            }
            active_queue = add_queue;
            add_queue.clear();
            //make active_queue empty
        }
        // Find requests in the current direction
        vector<IORequest> candidates;
        for(const auto& req : active_queue){
            if((direction == 1 && req.track_num >= current_track) ||
               (direction == -1 && req.track_num <= current_track)){
                candidates.push_back(req);
            }
        }
        if(candidates.empty()){
            // Reverse direction if no candidates found
            direction = -direction;
            for(const auto& req : active_queue){
                if((direction == 1 && req.track_num >= current_track) ||
                   (direction == -1 && req.track_num <= current_track)){
                    candidates.push_back(req);
                }
            }
        }
        auto it = min_element(candidates.begin(), candidates.end(),
            [current_track](const IORequest& a, const IORequest& b){
                return abs(a.track_num - current_track) < abs(b.track_num - current_track);
            });
        IORequest req = *it;
        // Remove from original list
        active_queue.erase(remove_if(active_queue.begin(), active_queue.end(),
            [req](const IORequest& r){ return r.request_id == req.request_id; }), active_queue.end());
        return req;
    }
};

IOScheduler* IOScheduler::getScheduler(char algo){
    switch(algo){
            case 'N':
                // printf("Using FIFO Scheduler as default.\n");
                return new FIFOScheduler();
            case 'S':
                // printf("Using SSTF Scheduler.\n");
                return new SSTFScheduler();
            case 'L':
                // printf("Using LOOK Scheduler.\n");
                return new LOOKScheduler();
            case 'C':
                // printf("Using CLOOK Scheduler.\n");
                return new CLOOKScheduler();
            case 'F':
                // printf("Using FLOOK Scheduler.\n");
                return new FLOOKScheduler();
            default:
                // cerr << "Unknown scheduling algorithm: " << algo << endl;
                exit(1);
    }
}

class Simuator{
    private:
    IOScheduler* scheduler;
    map<int,IORequest> requests;    
    public:
    Simuator(IOScheduler* sched): scheduler(sched){}

    void Simulation(){
        int current_time = 0;
        int current_track = 0;
        // int direction = 1; // 1 for up, -1 for down
        int n = io_requests.size();
        // printf("Starting simulation with %d IO requests.\n", n);

        while(!io_requests.empty()){        
            for(auto it = io_requests.begin(); it != io_requests.end(); ){
                if(it->issue_time <= current_time){
                    // arriving_requests.push_back(*it);
                    // printf("Time %d: Request ID %d arrived (Track %d)\n", current_time, it->request_id, it->track_num);
                    scheduler->addRequest(*it);
                    it = io_requests.erase(it);
                }
                else{
                    ++it;
                }
            }
            // printf("Time %d: Processing next request from track %d\n", current_time, current_track);
            IORequest req = scheduler->getNextRequest(current_track);
            // printf("Time %d: Selected Request ID %d (Track %d)\n", current_time, req.request_id, req.track_num);
            if(req.request_id==-1){
                // No requests available, advance time
                current_time+=1;
                // printf("Time %d: No requests to process, advancing time.\n", current_time);
                continue;
            }
            else{
            req.start_time = current_time;
            current_time += abs(req.track_num - current_track); // Simulate movement time
            req.end_time = current_time;
            
            // Update statistics
            total_time = current_time;
            tot_movement += abs(req.track_num - current_track);
            current_track = req.track_num;
            int turnaround = req.end_time - req.issue_time;
            int waittime = req.start_time - req.issue_time;
            avg_turnaround += turnaround;
            avg_waittime += waittime;
            if(waittime > max_waittime){
                max_waittime = waittime;
            }
            // Store completed request
            requests[req.request_id] = req;
            // printf("%5d: %5d %5d %5d\n", req.request_id, req.issue_time, req.start_time, req.end_time);

            }
        }

        while(true){
            IORequest req = scheduler->getNextRequest(current_track);
            if(req.request_id==-1){
                break;
            }
            req.start_time = current_time;
            current_time += abs(req.track_num - current_track); // Simulate movement time
            req.end_time = current_time;

            // Update statistics
            total_time = current_time;
            tot_movement += abs(req.track_num - current_track);
            current_track = req.track_num;
            int turnaround = req.end_time - req.issue_time;
            int waittime = req.start_time - req.issue_time;
            avg_turnaround += turnaround;
            avg_waittime += waittime;
            if(waittime > max_waittime){
                max_waittime = waittime;
            }
            // Store completed request
            // requests.push_back(req);
            requests[req.request_id] = req;
            // printf("%5d: %5d %5d %5d\n", req.request_id, req.issue_time, req.start_time, req.end_time);      
        }

        if(n > 0){
            avg_turnaround /= n;
            avg_waittime /= n;
        }
        io_utilization = (double)tot_movement / total_time;
    }

    void Summary(){
        for(const auto& req:requests){
             printf("%5d: %5d %5d %5d\n", req.second.request_id, req.second.issue_time, req.second.start_time, req.second.end_time);
        }
        printf("SUM: %d %d %.4lf %.2lf %.2lf %d\n", 
             total_time, tot_movement, io_utilization, 
                   avg_turnaround, avg_waittime, max_waittime);
    }
};

class InputManager{
    public:
    bool LoadInputFile(const string& filename){
        try{
            // printf("Loading input file: %s\n", filename.c_str());
            std::ifstream file(filename);
            // file.open(filename); // redundant line file alrady opened in above line
            if(!file.is_open()){
                // printf("-----line 541");
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
            int request_num = 0;
            while(true){
                string line = nextline();
                stringstream ss(line);
                if(line.empty()){
                    break;
                }
                int issue_time, track_num;
                ss >> issue_time >> track_num;
                // printf("Read IO Request - ID: %d, Issue Time: %d, Track Number: %d\n", request_num, issue_time, track_num);  
                IORequest req{request_num, issue_time, track_num};
                io_requests.push_back(req);
                request_num+=1;  
            }
            // printf(io_requests.size() == 0 ? "No IO requests found in the input file.\n" : "Finished loading IO requests from input file.\n");
            return true;
        }
        catch(const exception &e){
            // cerr << "Error opening input file: " << e.what() << endl;
            return false;
        }
    }
};

int main(int argc, char* argv[]){
    if (argc<2){
        // cout << "No arguments provided." << endl;
        return 1;
    } 
    else if(argc==2){
        // printf("No scheduling algorithm specified. Using default FIFO.\n");
        config.inputfile = argv[1];
        cout<<config.inputfile.c_str()<<endl;
    }
    else{
        // printf("Parsing command-line arguments.\n");
        int opt;
        if((opt = getopt(argc,argv,":s:"))!=-1){
            switch(opt){
                case 's':
                    config.algo = optarg[0];
                    config.inputfile = argv[2];
                    // printf("Scheduling algorithm: %c\n", config.algo);
                    break;
                default:
                    // cout << "unknown option." << endl;
                    return 1;
            }
        }

    }

    InputManager im;
    if(!im.LoadInputFile(config.inputfile)){
        // cout << "Failed to load input file: " << config.inputfile << endl;
        return 1;
    }

    IOScheduler* scheduler = IOScheduler::getScheduler(config.algo);
    Simuator sim(scheduler);
    sim.Simulation();
    sim.Summary();
    return 0;
}