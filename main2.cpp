#define BACKWARD_HAS_DW 1
#include "backward.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <csignal>
#include <ctime>
#include <sstream>

struct StackFrame {
    std::string file;
    int line;
    std::string function;
};

bool shouldSkipFrame(const std::string& filename, const std::string& function) {
    if (filename.find("backward.hpp") != std::string::npos) {
        return true;
    }
    
    if (filename.find("libc") != std::string::npos ||
        filename.find("csu/") != std::string::npos ||
        filename.find("sysdeps/") != std::string::npos) {
        return true;
    }
    
    if (function.find("__libc") != std::string::npos ||
        function.find("_start") == 0 ||
        function == "??") {
        return true;
    }
    
    if (filename == "??" || filename.empty()) {
        return true;
    }
    
    if (function.find("captureTrace") != std::string::npos ||
        function.find("printTrace") != std::string::npos ||
        function.find("handleCrash") != std::string::npos) {
        return true;
    }
    
    return false;
}

std::vector<StackFrame> captureTrace(int skip = 1) {
    backward::StackTrace st;
    st.load_here(32);
    backward::TraceResolver tr;
    tr.load_stacktrace(st);
    
    std::vector<StackFrame> frames;
    
    for (size_t i = skip; i < st.size(); ++i) {
        backward::ResolvedTrace trace = tr.resolve(st[i]);
        
        std::string filename = trace.source.filename;
        std::string function = trace.object_function;
        
        if (shouldSkipFrame(filename, function)) {
            continue;
        }
        
        StackFrame frame;
        frame.file = filename.empty() ? "??" : filename;
        frame.line = trace.source.line;
        frame.function = function.empty() ? "??" : function;
        
        frames.push_back(frame);
    }
    
    return frames;
}

std::string getCurrentTimestamp() {
    std::time_t now = std::time(nullptr);
    char buf[100];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return std::string(buf);
}

std::string getSignalName(int signal) {
    switch(signal) {
        case SIGSEGV: return "SIGSEGV (Segmentation fault)";
        case SIGABRT: return "SIGABRT (Abort)";
        case SIGFPE:  return "SIGFPE (Floating point exception)";
        case SIGILL:  return "SIGILL (Illegal instruction)";
        case SIGBUS:  return "SIGBUS (Bus error)";
        default:      return "Signal " + std::to_string(signal);
    }
}

void saveTraceToFile(const std::vector<StackFrame>& frames, int signal) {
    std::string filename = "crash_" + std::to_string(std::time(nullptr)) + ".log";
    std::ofstream file(filename);
    
    if (file.is_open()) {
        file << "=== CRASH REPORT ===\n";
        file << "Timestamp: " << getCurrentTimestamp() << "\n";
        file << "Signal: " << getSignalName(signal) << "\n";
        file << "\nStack trace (" << frames.size() << " frames):\n";
        
        for (size_t i = 0; i < frames.size(); ++i) {
            file << "  #" << i << " " 
                 << frames[i].file << ":" << frames[i].line 
                 << " in " << frames[i].function << "\n";
        }
        
        file << "\n=== END CRASH REPORT ===\n";
        file.close();
        
        std::cerr << "\n*** Crash log saved to: " << filename << " ***\n";
    }
}

void sendTraceToServer(const std::vector<StackFrame>& frames, int signal) {
    // Here you would implement your network call
    // For example: HTTP POST to your logging server
    
    std::cerr << "\n*** Sending crash report to server... ***\n";
    
    // Example: Build JSON payload
    std::ostringstream json;
    json << "{\n";
    json << "  \"timestamp\": \"" << getCurrentTimestamp() << "\",\n";
    json << "  \"signal\": \"" << getSignalName(signal) << "\",\n";
    json << "  \"stacktrace\": [\n";
    
    for (size_t i = 0; i < frames.size(); ++i) {
        json << "    {\n";
        json << "      \"frame\": " << i << ",\n";
        json << "      \"file\": \"" << frames[i].file << "\",\n";
        json << "      \"line\": " << frames[i].line << ",\n";
        json << "      \"function\": \"" << frames[i].function << "\"\n";
        json << "    }" << (i < frames.size() - 1 ? "," : "") << "\n";
    }
    
    json << "  ]\n";
    json << "}\n";
    
    std::cerr << "Payload:\n" << json.str() << "\n";
    
    // TODO: Implement actual HTTP POST
    // curl_easy_perform(...);
    // or system("curl -X POST ...");
}

void handleCrash(int signal) {
    std::cerr << "\n========================================\n";
    std::cerr << "FATAL ERROR: " << getSignalName(signal) << "\n";
    std::cerr << "========================================\n";
    
    // Capture the stack trace
    auto trace = captureTrace();
    
    // Print to console
    std::cerr << "\nStack trace (" << trace.size() << " frames):\n";
    for (size_t i = 0; i < trace.size(); ++i) {
        std::cerr << "  #" << i << " " 
                  << trace[i].file << ":" << trace[i].line 
                  << " in " << trace[i].function << "\n";
    }
    
    // Save to file
    saveTraceToFile(trace, signal);
    
    // Send to server (if you want)
    // sendTraceToServer(trace, signal);
    
    std::cerr << "\n========================================\n";
    
    // Re-raise the signal to allow default handling (core dump, etc.)
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

void installCrashHandler() {
    std::signal(SIGSEGV, handleCrash);
    std::signal(SIGABRT, handleCrash);
    std::signal(SIGFPE,  handleCrash);
    std::signal(SIGILL,  handleCrash);
    std::signal(SIGBUS,  handleCrash);
    
    std::cout << "Crash handler installed\n";
}

// Example functions
void level3() {
    std::cout << "In level3(), about to crash...\n";
    int* ptr = nullptr;
    *ptr = 42;  // CRASH!
}

void level2() {
    level3();
}

void level1() {
    level2();
}

int main() {
    std::cout << "backward-cpp crash handling example\n";
    std::cout << "====================================\n\n";
    
    // Install the crash handler
    installCrashHandler();
    
    std::cout << "Starting normal execution...\n\n";
    
    // This will crash and trigger the handler
    level1();
    
    std::cout << "This line will never be reached\n";
    return 0;
}