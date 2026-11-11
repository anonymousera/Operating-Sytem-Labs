#include <iostream>
#include <cctype>
#include <string>
#include <string.h>
#include <fstream>
#include <map>
#include <unordered_map>
#include <vector>
#include <iomanip>
#include <list>
using namespace std; 

static const int MAX_DEF_IN_MODULE = 16;
static const int MAX_USE_IN_MODULE = 16;
static const int MEMORY_SIZE = 512;
int summary_error = 0; // total number of errors
int summary_warning = 0; // total number of warnings

void __syntax_error(int errcode, int linenum, int lineoffset) { 
    const char* error_str[] = { 
        "TOO_MANY_DEF_IN_MODULE",     // > 16 
        "TOO_MANY_USE_IN_MODULE",     // > 16 
        "TOO_MANY_INSTR",             // total num_instr exceeds memory size (512) 
        "MARIE_EXPECTED",             // Addressing Expected which is M/A/R/I/E 
        "NUM_EXPECTED",               // Number expect, anything >= 2^30 is not a number either 
        "SYM_EXPECTED",               // Symbol Expected 
        "SYM_TOO_LONG"                // Symbol Name is too long  
    }; 
    printf("Syntax Error line %d offset %d: %s\n", linenum, lineoffset, error_str[errcode]); 
    exit(1); // this ensures that you exit on every SYNTAX error  
} 

const char* delimiter = " \t\n\r";
int linenum = 0;
int lineoffset = 0;
char* ptr = NULL;                 // pointer to start of current line
std::ifstream input_file;

void reset_offsets(){
    input_file.clear();
    input_file.seekg(0);
    linenum = 0;
    lineoffset = 0;
    ptr = NULL;
}

char* getToken() {
    static std::string str;  // stores current line
    char* tokenized_string;

    while (true) {
        if (ptr == NULL) {
            // read new line
            if (!std::getline(input_file, str)) 
            {   lineoffset+=1;
                return strdup("-1"); // EOF
            }
            linenum++;
            free(ptr);
            ptr = strdup(str.c_str());   
            tokenized_string = strtok(ptr, delimiter);
        }
        else {
            tokenized_string = strtok(NULL, delimiter); // subsequent calls
        }
        if (tokenized_string) {
            lineoffset = (int)(tokenized_string - ptr) + 1;
            char* ret_token = strdup(tokenized_string);
            return ret_token; 
        }

        else {
            // finished current line, move to next
            free(ptr);
            ptr = NULL;
        }
    }
    return NULL;
}


int readInt() { 
    char *tok = getToken(); 
    if (strcmp(tok, "-1") == 0) return -1; // End of file reached
    if (tok == NULL) __syntax_error(4, linenum, lineoffset);  //4=NUM_EXPECTED
    for (int i=0; tok[i] != '\0'; i++) { 
        if (tok[i] < '0' || tok[i] > '9') __syntax_error(4, linenum, lineoffset); 
    }
    long val = atol(tok); 
    if (val < 0 || val >= (1<<30)) __syntax_error(4, linenum, lineoffset); 
    return (int)val;
}


char* readSym(){
    char* tok = getToken();
    if (tok == NULL) __syntax_error(5, linenum, lineoffset);  //5 = SYM_EXPECTED
    if(!std::isalpha((unsigned char)tok[0])) {
        __syntax_error(5, linenum, lineoffset);
    }
    for(int i=1; tok[i] != '\0'; i++){
        if (!std::isalnum(tok[i]))  __syntax_error(5, linenum, lineoffset); // Checks if the character is not alphanumeric
    }
    return tok;
}


void checkMARIE(char ch){
    if(ch !='M' && ch !='A' && ch !='R' && ch !='I' && ch !='E')
    __syntax_error(3, linenum, lineoffset);
}

char* readMARIE(){
    char* tok = getToken();
    if (tok == NULL) __syntax_error(3, linenum, lineoffset);  //3=MARIE_EXPECTED
    int i=0;
    while(tok[i] !='\0'){
        checkMARIE(tok[i]);
        i++;
        if(i>1) __syntax_error(3, linenum, lineoffset);
    }
    return tok;
}

struct Symbol {
    int rel_add_in_mod;
    int overall_add; // base+relative address
    int module_num;
    bool defined_multiple;
    bool used;

    Symbol(): rel_add_in_mod(0), overall_add(0), module_num(0), defined_multiple(false), used(false){};
};

std::map<std::string,Symbol> symbol_map;
std::list<std::string> symbol_order;


void createSymbol(const char* sym,int rel_add,int base_add,int module){
    std::string key(sym);

    if(symbol_map.count(key)) {
        Symbol &existing_symbol = symbol_map[key];
        existing_symbol.defined_multiple = true;
        cout << "Warning: Module "<< module <<": " << key <<" redefinition ignored" << endl;
        summary_warning +=1;
        return;
    }
    symbol_order.push_back(sym);
    Symbol s;
    s.rel_add_in_mod = rel_add;
    s.overall_add = rel_add+base_add;
    s.module_num = module;
    symbol_map[sym] = s;
    return;
}

std::map<int, int> mod_map; //mapping module number to its base address


void pass1() { 
    int module = 0;
    int base_add = 0;

    while (true) { // new module starts

        int defcount = readInt();  
        if (defcount < 0) break; // eof reached 
        if (defcount > MAX_DEF_IN_MODULE) __syntax_error(0, linenum, lineoffset); //0=TOO_MANY_DEF_IN_MODULE

        for (int i=0;i<defcount;i++) { 
            char* sym = readSym(); 
            int rel_add = readInt();  // relative address of sym
            createSymbol(sym,rel_add,base_add, module);               
        } 

        int usecount = readInt();
        if (usecount > MAX_USE_IN_MODULE) __syntax_error(1, linenum, lineoffset); //1=TOO_MANY_USE_IN_MODULE
        for (int i=0;i<usecount;i++) { 
            readSym();
            //char* sym = readSym();  
        } 

        int instcount = readInt(); 
        if(instcount + base_add > MEMORY_SIZE) __syntax_error(2, linenum, lineoffset); //2=TOO_MANY_INSTR
        for (int i=0;i<instcount;i++) { 
            readMARIE();
            readInt();
            //char* addressmode = readMARIE();
            //int  operand = readInt(); 
        }

        mod_map[module] = base_add;
        for(auto it = symbol_map.begin(); it != symbol_map.end(); ++it){
            if(it->second.module_num == module){
                if(it->second.rel_add_in_mod >= instcount){
                    cout << "Warning: Module " << module << ": " << it->first << "=" << it->second.rel_add_in_mod << " valid=[0.." << instcount-1 << "] assume zero relative" << endl;
                    summary_warning +=1;
                    it->second.rel_add_in_mod = 0;
                    it->second.overall_add = base_add; // set to base address of the module
                }
            }
        }
        //for next module update base address and module number
        base_add +=instcount;
        module+=1;
    } 
} 

std::map<std::string,int> marie_map;
std::string init_code = "000";

void increment_init_code(){
    int code = std::stoi(init_code);
    code +=1;
    init_code = std::to_string(code);
    while(init_code.length()<3){
        init_code = "0" + init_code;
    }
    return;
}

void marie_instrs(const std::vector<char*> &uselist,std::vector<bool> &used_symbol,char* addressmode, int instruct_num, int base_add, int instcount){
        
        std::string key(addressmode);

        int opcode= (instruct_num)/1000;

        if (opcode >= 10) {
        marie_map[init_code] = 9999;
        cout << init_code << ": " << setw(4) << setfill('0') << marie_map[init_code] <<" Error: Illegal opcode; treated as 9999" << endl;
        summary_error +=1;
        increment_init_code();
        return;}        
        
        unsigned int operand = (instruct_num)%1000;

        if(key =="I"){
            if(operand >= 900){
                marie_map[init_code] = opcode*1000 + 999;
                cout << init_code << ": " << setw(4) << setfill('0') << marie_map[init_code] <<" Error: Illegal immediate operand; treated as 999" << endl;
                summary_error +=1;
            }
            else{
                marie_map[init_code] = instruct_num;
            cout << init_code << ": " << setw(4) << setfill('0')  << marie_map[init_code]<<endl;
            }
        }
        else if(key =="E"){
            if(operand >= uselist.size()){
                marie_map[init_code] = opcode*1000 + base_add;
                cout << init_code << ": " << setw(4) << setfill('0') << marie_map[init_code] <<" Error: External operand exceeds length of uselist; treated as relative=0" << endl;
                summary_error +=1;
                increment_init_code();
                return;
            }
            std::string symbol= uselist[operand];
            used_symbol[operand] = true; // mark as used
            if(!symbol_map.count(symbol)){
                marie_map[init_code] = opcode*1000;
                cout << init_code << ": " << setw(4) << setfill('0') << marie_map[init_code] <<" Error: "<< symbol << " is not defined; zero used" << endl;
                summary_error +=1;
            }
            else{
            marie_map[init_code] = (opcode)*1000 + symbol_map[symbol].overall_add;
            cout << init_code << ": " << setw(4) << setfill('0') << marie_map[init_code]<<endl;
            }
        }
        else if(key=="A"){
            if(operand >= MEMORY_SIZE){
                marie_map[init_code] = opcode*1000;
                cout << init_code <<": " << setw(4) << setfill('0') << marie_map[init_code] <<" Error: Absolute address exceeds machine size; zero used" << endl;
                summary_error +=1;
            }
            else{
                marie_map[init_code] = instruct_num;
                cout << init_code << ": " << setw(4) << setfill('0') << marie_map[init_code]<<endl;
            }
        }
        else if(key=="R"){
            if((int)operand >= instcount){
                marie_map[init_code] = opcode*1000 + base_add;
                cout << init_code << ": " << setw(4) << setfill('0') << marie_map[init_code] <<" Error: Relative address exceeds module size; relative zero used" << endl;
                summary_error +=1;
            }
            else{
                marie_map[init_code] = opcode*1000 + operand + base_add;
                cout << init_code << ": " << setw(4) << setfill('0') << marie_map[init_code]<<endl;
            }
        }
        else if(key == "M"){
            if(operand < 0 || operand >= mod_map.size()){
                marie_map[init_code] = opcode*1000 + mod_map[0];// mod_map[0] is the base address of the first module
                cout << init_code << ": " << setw(4) << setfill('0') << marie_map[init_code] <<" Error: Illegal module operand ; treated as module=0" << endl;
                summary_error +=1;
            }
            else{
            marie_map[init_code] = opcode*1000 + mod_map[operand];// 
            cout << init_code << ": " << setw(4) << setfill('0') << marie_map[init_code]<<endl;
            }
        }
        increment_init_code();
    return;
}


void pass2() { 
    int module = 0;
    int base_add = 0;

    while (true) { // this is a new module
        std::vector<char*> uselist;

        int defcount = readInt();  
        if (defcount < 0) break; // eof reached 
        if (defcount > MAX_DEF_IN_MODULE) __syntax_error(0, linenum, lineoffset); //0=TOO_MANY_DEF_IN_MODULE

        for (int i=0;i<defcount;i++) { 
            readSym();
            readInt();
            //char* sym = readSym(); 
            //int val = readInt();  // val is the relative address of sym
        } 

        int usecount = readInt();
        if (usecount > MAX_USE_IN_MODULE) __syntax_error(1, linenum, lineoffset); //1=TOO_MANY_USE_IN_MODULE
        for (int i=0;i<usecount;i++) { 
            char* sym = readSym(); 
            std::string key(sym);
            if(symbol_map.count(key)){  // ensure symbol exists in map
                symbol_map[sym].used = true;
            }
            uselist.push_back(sym);         
        } 

        std::vector<bool> used_symbol(usecount, false); // to track which symbols in the use list are actually used
        int instcount = readInt(); 
        if(instcount + base_add > MEMORY_SIZE) __syntax_error(3, linenum, lineoffset); //3=TOO_MANY_INSTR
        for (int i=0;i<instcount;i++) { 
            char* addressmode = readMARIE();
            int  instruct_num = readInt(); 
            marie_instrs(uselist, used_symbol, addressmode, instruct_num, base_add, instcount); 
            } 
        for(int i=0; i<usecount; i++){
            if(!used_symbol[i]){
                cout << "Warning: Module " << module << ": uselist[" << i << "]=" << uselist[i]<<" was not used" << endl;
                summary_warning +=1;
            }
        }
        base_add +=instcount;
        module+=1;
    } 
} 

void print_sym_table(){
    pass1();
    printf("SymTable:\n");
    for (const auto& sym : symbol_order) {
        const Symbol& s = symbol_map[sym];
        cout << sym << "=" << s.overall_add;
        if (s.defined_multiple) {
            cout << " Error: This variable is multiple times defined; first value used";
            summary_error++;
        }
        cout << endl;
    }
}

void print_binary_code(){
    reset_offsets();
    printf("\nBinaryCode:\n");
    pass2();
    bool flag=false;
    for(const auto& sym : symbol_order){
        const Symbol& s = symbol_map[sym];
        if(!s.used){
            if(!flag){
                cout << endl;
                flag = true;
            }
            cout << "Warning: Module " << s.module_num << ": " << sym <<" was defined but never used" << endl;
            summary_warning +=1;
        }
    }
}

int main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(true);  // sync cout/cerr with printf
    std::cout.setf(std::ios::unitbuf); // disables buffering for cout

    if (argc < 2) {
        return 1; // error
    }
    std::string filename = argv[1];

    input_file.open(filename);
    if (!input_file.is_open()) {
        return 1; // error
    }

    print_sym_table();

    print_binary_code();

    cout << "\nSummary: Errors=" << summary_error << " Warnings=" << summary_warning << endl;
    input_file.close();
    free(ptr);
}
