#define BACKWARD_HAS_DW 1
#include "backward.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <csignal>
#include <ctime>
#include <sstream>
#include <unistd.h>
#include <curl/curl.h>

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
        function.find("handleCrash") != std::string::npos ||
        function.find("saveTrace") != std::string::npos ||
        function.find("sendTrace") != std::string::npos) {
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
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGFPE:  return "SIGFPE";
        case SIGILL:  return "SIGILL";
        case SIGBUS:  return "SIGBUS";
        default:      return "UNKNOWN";
    }
}

std::string getHostname() {
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    return std::string(hostname);
}

std::string escapeJSON(const std::string& str) {
    std::ostringstream escaped;
    for (char c : str) {
        switch(c) {
            case '"':  escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:   escaped << c; break;
        }
    }
    return escaped.str();
}

std::string buildJSONPayload(const std::vector<StackFrame>& frames, int signal) {
    std::ostringstream json;
    
    json << "{\n";
    json << "  \"timestamp\": \"" << escapeJSON(getCurrentTimestamp()) << "\",\n";
    json << "  \"signal\": \"" << getSignalName(signal) << "\",\n";
    json << "  \"signal_code\": " << signal << ",\n";
    json << "  \"pid\": " << getpid() << ",\n";
    json << "  \"hostname\": \"" << escapeJSON(getHostname()) << "\",\n";
    json << "  \"user\": \"" << escapeJSON(getenv("USER") ? getenv("USER") : "unknown") << "\",\n";
    json << "  \"stacktrace\": [\n";
    
    for (size_t i = 0; i < frames.size(); ++i) {
        json << "    {\n";
        json << "      \"frame\": " << i << ",\n";
        json << "      \"file\": \"" << escapeJSON(frames[i].file) << "\",\n";
        json << "      \"line\": " << frames[i].line << ",\n";
        json << "      \"function\": \"" << escapeJSON(frames[i].function) << "\"\n";
        json << "    }" << (i < frames.size() - 1 ? "," : "") << "\n";
    }
    
    json << "  ]\n";
    json << "}\n";
    
    return json.str();
}

// Callback for curl - ignores response
size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    // Just ignore the response
    return size * nmemb;
}

bool sendTraceToServer(const std::string& url, const std::string& jsonPayload) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Error: Failed to initialize curl\n";
        return false;
    }
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonPayload.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);  // 5 seconds timeout
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);  // 2 seconds connect timeout
    
    CURLcode res = curl_easy_perform(curl);
    
    bool success = (res == CURLE_OK);
    if (!success) {
        std::cerr << "Error: curl_easy_perform() failed: " 
                  << curl_easy_strerror(res) << "\n";
    } else {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        std::cerr << "Server responded with HTTP " << response_code << "\n";
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return success;
}

void saveTraceToFile(const std::vector<StackFrame>& frames, int signal) {
    std::string filename = "crash_" + std::to_string(std::time(nullptr)) + ".log";
    std::ofstream file(filename);
    
    if (file.is_open()) {
        file << "=== CRASH REPORT ===\n";
        file << "Timestamp: " << getCurrentTimestamp() << "\n";
        file << "Signal: " << getSignalName(signal) << " (" << signal << ")\n";
        file << "PID: " << getpid() << "\n";
        file << "Hostname: " << getHostname() << "\n";
        file << "User: " << (getenv("USER") ? getenv("USER") : "unknown") << "\n";
        file << "\nStack trace (" << frames.size() << " frames):\n";
        
        for (size_t i = 0; i < frames.size(); ++i) {
            file << "  #" << i << " " 
                 << frames[i].file << ":" << frames[i].line 
                 << " in " << frames[i].function << "\n";
        }
        
        file << "\n=== END CRASH REPORT ===\n";
        file.close();
        
        std::cerr << "*** Crash log saved to: " << filename << " ***\n";
    }
}

void handleCrash(int signal) {
    std::cerr << "\n========================================\n";
    std::cerr << "FATAL ERROR: " << getSignalName(signal) << " (" << signal << ")\n";
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
    
    // Save to local file
    saveTraceToFile(trace, signal);
    
    // Build JSON payload
    std::string jsonPayload = buildJSONPayload(trace, signal);
    
    // Send to server (change URL to your server)
    std::cerr << "\n*** Sending crash report to server... ***\n";
    const char* server_url = getenv("CRASH_REPORT_URL");
    if (server_url) {
        bool sent = sendTraceToServer(server_url, jsonPayload);
        if (sent) {
            std::cerr << "*** Crash report sent successfully ***\n";
        } else {
            std::cerr << "*** Failed to send crash report ***\n";
        }
    } else {
        std::cerr << "*** CRASH_REPORT_URL not set, skipping server upload ***\n";
        std::cerr << "*** (Set environment variable to enable) ***\n";
    }
    
    std::cerr << "\n========================================\n";
    
    // Re-raise the signal to allow default handling
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

// Example functions to create a call stack
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
    std::cout << "backward-cpp crash handling with HTTP upload\n";
    std::cout << "============================================\n\n";
    
    // Install the crash handler
    installCrashHandler();
    
    std::cout << "Starting normal execution...\n\n";
    
    // This will crash and trigger the handler
    level1();
    
    std::cout << "This line will never be reached\n";
    return 0;
}