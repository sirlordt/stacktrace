#define BACKWARD_HAS_DW 1
#include "backward.hpp"

#include <iostream>
#include <vector>
#include <string>

namespace backward { 
    backward::SignalHandling sh;
} // namespace backward

struct StackFrame {
    std::string file;
    int line;
    std::string function;
};

bool shouldSkipFrame(const std::string& filename, const std::string& function) {
    // Skip backward-cpp internals
    if (filename.find("backward.hpp") != std::string::npos) {
        return true;
    }
    
    // Skip libc/system frames
    if (filename.find("libc") != std::string::npos ||
        filename.find("csu/") != std::string::npos ||
        filename.find("sysdeps/") != std::string::npos) {
        return true;
    }
    
    // Skip system functions
    if (function.find("__libc") != std::string::npos ||
        function.find("_start") == 0 ||
        function == "??") {  // <-- Esta línea ya lo debería filtrar
        return true;
    }
    
    // Skip unresolved frames (??:0)
    if (filename == "??" || filename.empty()) {  // <-- Agregar esta línea
        return true;
    }
    
    // Skip our own tracing infrastructure
    if (function.find("captureTrace") != std::string::npos ||
        function.find("printTrace") != std::string::npos) {
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

void printTrace(const std::vector<StackFrame>& frames) {
    std::cout << "Stack trace (" << frames.size() << " frames):\n";
    for (size_t i = 0; i < frames.size(); ++i) {
        std::cout << "  #" << i << " " 
                  << frames[i].file << ":" << frames[i].line 
                  << " in " << frames[i].function << "\n";
    }
}

// Example functions to create a call stack
void level3() {
    std::cout << "\n=== Capturing trace from level3() ===\n";
    auto trace = captureTrace();
    printTrace(trace);
}

void level2() {
    level3();
}

void level1() {
    level2();
}

int main() {
    std::cout << "backward-cpp minimal example\n";
    std::cout << "============================\n";
    
    // Example 1: Simple trace
    std::cout << "\n=== Example 1: Direct capture ===\n";
    auto trace = captureTrace();
    printTrace(trace);
    
    // Example 2: Nested function calls
    std::cout << "\n=== Example 2: Nested calls ===\n";
    level1();
    
    // Example 3: Handling crashes (comment out to test)
    // This will print a nice stack trace when it crashes
    // int* ptr = nullptr;
    // *ptr = 42;  // This will crash and show the stack trace
    
    std::cout << "\n=== Program completed successfully ===\n";
    return 0;
}