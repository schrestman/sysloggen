#include <iostream>     // For input/output operations (e.g., cout, cerr)
#include <thread>       // For multi-threading capabilities
#include <vector>       // For using dynamic arrays (e.g., to store messages, hostnames, threads)
#include <random>       // For generating random numbers (e.g., for selecting messages, hostnames, or random strings)
#include <string>       // For string manipulation
#include <sys/socket.h> // For socket programming (e.g., socket(), sendto())
#include <netinet/in.h> // For internet address structures (e.g., sockaddr_in)
#include <arpa/inet.h>  // For IP address manipulation (e.g., inet_pton)
#include <cstring>      // For memory manipulation functions (e.g., memset)
#include <unistd.h>     // For POSIX operating system API (e.g., close() for sockets)
#include <chrono>       // For time measurements and high-resolution clock
#include <cstdlib>      // For general utilities (e.g., atoi, atol, exit)
//#include <format>       // For C++20 string formatting (if available, otherwise use stringstream or manual concatenation)
#include <iomanip>      // For output formatting (e.g., std::put_time, std::setw, std::setfill)
#include <fstream>      // For file input/output operations (e.g., ifstream)
#include <algorithm>    // For algorithms (e.g., std::shuffle, though not used in final version)
#include <ctime>        // For time functions (e.g., std::time, std::localtime, std::mktime, std::strftime)
#include <sstream>      // For string stream manipulation (e.g., std::ostringstream)

// Using the standard namespace to avoid prefixing std::
using namespace std;

// Global vector to store syslog messages read from a file.
// This allows all threads to access the same pool of messages.
vector<string> syslogMessages;
// Global vector to store hostnames read from a host file.
// This allows all threads to select from a predefined list of hostnames.
vector<string> hostnames;
// Global vector to store source IP addresses read from a file.
vector<string> source_ips;
// Global random device and generator for selecting messages/hostnames.
// Using a single global generator might have contention issues in high-concurrency
// but for simple random access it's often acceptable. For string generation,
// thread_local is used.
random_device rd;
mt19937 gen(rd()); // Mersenne Twister engine seeded by random_device

// Function to generate a random string of a specified length.
// This function is thread-local for its random number generation to avoid
// contention and improve performance in multi-threaded environments.
string generateRandomString(int len) {
    // thread_local static ensures that each thread has its own instance
    // of rd_str and gen_str. This prevents race conditions and ensures
    // better randomness distribution across threads.
    thread_local static std::random_device rd_str;
    thread_local static std::mt19937 gen_str(rd_str());
    // Character set for random string generation, including alphanumeric and space.
    static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ";
    string s(len, '\0'); // Initialize string with null characters of specified length
    for (int i = 0; i < len; ++i) {
        // Pick a random character from the alphanum array
        s[i] = alphanum[gen_str() % (sizeof(alphanum) - 1)];
    }
    return s;
}

// Function to generate a timestamp in RFC 3164 format (Mmm dd hh:mm:ss).
// RFC 3164 is a common format for legacy syslog messages.
string generateRFC3164Timestamp() {
    auto now = chrono::system_clock::now(); // Get current time point
    time_t now_c = chrono::system_clock::to_time_t(now); // Convert to C-style time_t
    tm* ltm = localtime(&now_c); // Convert to local time structure (not thread-safe, but often okay for display)

    ostringstream oss; // Output string stream to build the timestamp string
    // Format the time according to RFC 3164 specifications:
    // %b: Abbreviated month name (e.g., Jul)
    // %e: Day of the month, space-padded for single digits (e.g., " 2" instead of "02")
    // %H: Hour in 24h format (00-23)
    // %M: Minute (00-59)
    // %S: Second (00-61)
    oss << put_time(ltm, "%b %e %H:%M:%S");

    return oss.str(); // Return the formatted timestamp string
}

// Function to compose a complete syslog message.
// It can use messages from a file, generate random ones, and select hostnames from a file.
string getSyslogMessage() {
    string message_content;  // Stores the actual content of the syslog message
    string current_hostname; // Stores the hostname for the syslog message

    // Determine the <MESSAGE> part of the syslog message.
    // If syslogMessages vector is not empty (i.e., messages were loaded from a file),
    // randomly select one from the loaded messages.
    if (!syslogMessages.empty()) {
        uniform_int_distribution<> distrib_msg(0, syslogMessages.size() - 1);
        message_content = syslogMessages[distrib_msg(gen)]; // Select a random message from the vector
    } else {
        // If no messages were loaded from a file, generate a random string as the message content.
        message_content = generateRandomString(50);
    }

    // Determine the hostname part of the syslog message.
    // If hostnames vector is not empty (i.e., hostnames were loaded from a file),
    // randomly select one from the loaded hostnames.
    if (!hostnames.empty()) {
        uniform_int_distribution<> distrib_host(0, hostnames.size() - 1);
        current_hostname = hostnames[distrib_host(gen)]; // Select a random hostname from the vector
    } else {
        // If no hostnames were loaded, use a default hostname.
        current_hostname = "myhost";
    }

    // RFC 3164 syslog message fields:
    int facility = 1; // Example: LOG_USER (Facility codes define the source of the message)
    int severity = 3; // Example: LOG_ERR (Severity codes define the importance of the message)
    // Priority (PRI) is calculated as (facility * 8) + severity.
    int pri = (facility * 8) + severity;

    string timestamp = generateRFC3164Timestamp(); // Get the formatted RFC 3164 timestamp
    // current_hostname is already determined above

    string app_tag = "sysloggen"; // Application name or TAG (identifies the process that originated the message)
    // Optionally, you can add PID (Process ID) to the tag for some RFC 3164 implementations:
    // string app_tag = "sysloggen[" + to_string(getpid()) + "]";

    // Construct the full RFC 3164 message string using std::format (C++20).
    // Format: <PRI>TIMESTAMP HOSTNAME TAG: MESSAGE
    // Example: <13>Jul  2 10:18:14 myhost sysloggen: This is the message.
    // Note the space padding for the day if it's a single digit (e.g., " 2" vs "12").
    std::ostringstream oss_msg;
    oss_msg << "<" << pri << ">" << timestamp << " " << current_hostname << " " << app_tag << ": " << message_content << "\n";
    return oss_msg.str();
}

// Function to send a single syslog message over a UDP socket.
void sendSyslogMessage(int sock, const sockaddr_in& addr) {
    string message = getSyslogMessage(); // Get the complete syslog message string
    ssize_t len = message.length();       // Get the length of the message

    // Print the message to standard output for debugging/monitoring.
    cout << "Sending: " << message;

    // Send the message using sendto().
    // The message is sent as a UDP datagram to the specified address.
    // We don't check the return value of sendto for performance reasons in this
    // high-speed generator. In a high-speed scenario, a few dropped messages
    // are often acceptable for maximizing throughput.
    sendto(sock, message.c_str(), len, 0, (sockaddr*)&addr, sizeof(addr));
}

// Function to print the correct usage instructions for the program.
void printUsage(const char* progName) {
    cerr << "Usage: " << progName << " <Destination_IP> <Port> <NumMessages> <NumThreads> [ -s <Source_IP> | -S <source_ip_file> ] [ -f <message_file> ] [ -h <host_file> ] [ -d <delay_ms> ]" << endl;
    cerr << "  <Destination_IP>: IP address of the syslog receiver." << endl;
    cerr << "  <Port>: Port number of the syslog receiver (e.g., 514)." << endl;
    cerr << "  <NumMessages>: Total number of messages to send." << endl;
    cerr << "  <NumThreads>: Number of concurrent threads to use." << endl;
    cerr << "  -s <Source_IP>: (Optional) Specifies the source IP address for the outgoing packets. If not provided, the OS determines the source IP." << endl;
    cerr << "  -S <source_ip_file>: (Optional) Path to a file containing source IP addresses, one per line. Cannot be used with -s." << endl;
    cerr << "  -f <message_file>: (Optional) Path to a file containing messages, one per line." << endl;
    cerr << "  -h <host_file>: (Optional) Path to a file containing hostnames, one per line." << endl;
    cerr << "  -d <delay_ms>: (Optional) Adds a delay in milliseconds between each syslog message." << endl;
    exit(EXIT_FAILURE); // Exit the program indicating an error
}

// Main function where the program execution begins.
int main(int argc, char* argv[]) {
    // Check if the minimum number of command-line arguments are provided.
    // Expected: program_name, dest_ip, port, num_messages, num_threads (5 arguments)
    if (argc < 5) {
        printUsage(argv[0]); // Print usage and exit if arguments are insufficient
    }

    // Parse mandatory command-line arguments.
    const char* dest_ip = argv[1];       // Destination IP address of the syslog receiver
    int dest_port = atoi(argv[2]);       // Destination port number
    long num_messages = atol(argv[3]);   // Total number of messages to send
    int num_threads = atoi(argv[4]);     // Number of concurrent threads

    string sourceIpString; // Stores the optional source IP address
    string sourceIpFilePath; // Stores the optional path to the source IP file
    string messageFilePath; // Stores the optional path to the message file
    string hostFilePath;    // Stores the optional path to the host file
    int delay_ms = 0;       // Stores the optional delay in milliseconds

    // Parse optional command-line arguments (-s, -f, -h).
    for (int i = 5; i < argc; ++i) {
        if (string(argv[i]) == "-s") {
            // If -s is found, the next argument should be the source IP.
            if (i + 1 < argc) {
                sourceIpString = argv[i+1];
                i++; // Increment i to skip the IP address argument in the next iteration
            } else {
                cerr << "Error: -s option requires an IP address." << endl;
                printUsage(argv[0]);
            }
        } else if (string(argv[i]) == "-S") {
            // If -S is found, the next argument should be the source IP file path.
            if (i + 1 < argc) {
                sourceIpFilePath = argv[i+1];
                i++; // Increment i to skip the filename argument
            } else {
                cerr << "Error: -S option requires a filename." << endl;
                printUsage(argv[0]);
            }
        } else if (string(argv[i]) == "-f") {
            // If -f is found, the next argument should be the message file path.
            if (i + 1 < argc) {
                messageFilePath = argv[i+1];
                i++; // Increment i to skip the filename argument
            } else {
                cerr << "Error: -f option requires a filename." << endl;
                printUsage(argv[0]);
            }
        } else if (string(argv[i]) == "-h") {
            // If -h is found, the next argument should be the host file path.
            if (i + 1 < argc) {
                hostFilePath = argv[i+1];
                i++; // Increment i to skip the filename argument
            } else {
                cerr << "Error: -h option requires a filename." << endl;
                printUsage(argv[0]);
            }
        } else if (string(argv[i]) == "-d") {
            if (i + 1 < argc) {
                delay_ms = atoi(argv[++i]);
            } else {
                cerr << "Error: -d option requires a number in milliseconds." << endl;
                printUsage(argv[0]);
            }
        } else {
            // Handle any unexpected arguments, or just warn and ignore.
            cerr << "Warning: Unknown argument: " << argv[i] << endl;
        }
    }

    // Read messages from the specified file if messageFilePath is not empty.
    if (!messageFilePath.empty()) {
        ifstream inputFile(messageFilePath); // Open the message file for reading
        if (!inputFile.is_open()) {
            cerr << "Error: Could not open message file: " << messageFilePath << endl;
            return EXIT_FAILURE; // Exit if file cannot be opened
        }
        string line;
        while (getline(inputFile, line)) { // Read line by line
            syslogMessages.push_back(line); // Add each line as a syslog message
        }
        inputFile.close(); // Close the file

        if (syslogMessages.empty()) {
            cerr << "Warning: Message file is empty. Will generate random messages for <MESSAGE> part." << endl;
        }
    }

    // Read hostnames from the specified file if hostFilePath is not empty.
    if (!hostFilePath.empty()) {
        ifstream hostFile(hostFilePath); // Open the host file for reading
        if (!hostFile.is_open()) {
            cerr << "Error: Could not open host file: " << hostFilePath << endl;
            return EXIT_FAILURE; // Exit if file cannot be opened
        }
        string line;
        while (getline(hostFile, line)) { // Read line by line
            hostnames.push_back(line); // Add each line as a hostname
        }
        hostFile.close(); // Close the file

        if (hostnames.empty()) {
            cerr << "Warning: Host file is empty. Will use 'myhost' as hostname." << endl;
        }
    }

    // Read source IPs from the specified file if sourceIpFilePath is not empty.
    if (!sourceIpFilePath.empty()) {
        if (!sourceIpString.empty()) {
            cerr << "Error: -s and -S options cannot be used together." << endl;
            printUsage(argv[0]);
        }
        ifstream sourceIpFile(sourceIpFilePath); // Open the source IP file for reading
        if (!sourceIpFile.is_open()) {
            cerr << "Error: Could not open source IP file: " << sourceIpFilePath << endl;
            return EXIT_FAILURE; // Exit if file cannot be opened
        }
        string line;
        while (getline(sourceIpFile, line)) { // Read line by line
            source_ips.push_back(line); // Add each line as a source IP
        }
        sourceIpFile.close(); // Close the file

        if (source_ips.empty()) {
            cerr << "Warning: Source IP file is empty. Will use OS-assigned source IP." << endl;
        }
    }

    // Record the start time for performance measurement.
    auto start_time = std::chrono::high_resolution_clock::now();

    vector<thread> threads; // Vector to hold our worker threads
    // Calculate how many messages each thread should send.
    long messages_per_thread = num_messages / num_threads;
    long remainder = num_messages % num_threads; // Account for any leftover messages

    // Create and launch threads.
    for (int i = 0; i < num_threads; ++i) {
        // The last thread handles any remainder messages to ensure all messages are sent.
        int num_messages_for_thread = (i == num_threads - 1) ? (messages_per_thread + remainder) : messages_per_thread;

        // Emplace a new thread into the vector.
        // The lambda function defines the work each thread will do.
        threads.emplace_back([dest_ip, dest_port, num_messages_for_thread, sourceIpString, delay_ms]() {
            // Set up the destination address structure once.
            sockaddr_in dest_addr;
            memset(&dest_addr, 0, sizeof(dest_addr));
            dest_addr.sin_family = AF_INET;
            if (inet_pton(AF_INET, dest_ip, &dest_addr.sin_addr) <= 0) {
                cerr << "Error: Invalid destination IP address: " << dest_ip << endl;
                return;
            }
            dest_addr.sin_port = htons(dest_port);

            // Loop to send the assigned number of messages for this thread.
            for (int j = 0; j < num_messages_for_thread; ++j) {
                int sock = socket(AF_INET, SOCK_DGRAM, 0);
                if (sock == -1) {
                    cerr << "Error: Could not create socket in thread." << endl;
                    continue; // Skip this message
                }

                string currentSourceIp = sourceIpString;
                if (!source_ips.empty()) {
                    uniform_int_distribution<> distrib_ip(0, source_ips.size() - 1);
                    currentSourceIp = source_ips[distrib_ip(gen)];
                }

                if (!currentSourceIp.empty()) {
                    sockaddr_in local_addr;
                    memset(&local_addr, 0, sizeof(local_addr));
                    local_addr.sin_family = AF_INET;
                    if (inet_pton(AF_INET, currentSourceIp.c_str(), &local_addr.sin_addr) <= 0) {
                        cerr << "Error: Invalid source IP address: " << currentSourceIp << endl;
                        close(sock);
                        continue; // Skip this message
                    }
                    local_addr.sin_port = htons(0);

                    int freebind = 1;
                    if (setsockopt(sock, IPPROTO_IP, IP_FREEBIND, &freebind, sizeof(freebind)) == -1) {
                        cerr << "Warning: Could not set IP_FREEBIND socket option: " << strerror(errno) << endl;
                        // Continue anyway, as this might not be a fatal error depending on the system configuration.
                    }

                    if (bind(sock, (sockaddr*)&local_addr, sizeof(local_addr)) == -1) {
                        cerr << "Error: Could not bind socket to source IP " << currentSourceIp << ": " << strerror(errno) << endl;
                        close(sock);
                        continue; // Skip this message
                    }
                }

                sendSyslogMessage(sock, dest_addr);
                if (delay_ms > 0) {
                    this_thread::sleep_for(chrono::milliseconds(delay_ms));
                }
                close(sock);
            }
        });
    }

    // Join all threads.
    // The main thread waits for all worker threads to complete their execution.
    for (auto& thread : threads) {
        thread.join();
    }

    // Record the end time and calculate the total duration.
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(end_time - start_time);

    // Calculate messages per second.
    double messages_per_second = static_cast<double>(num_messages) / duration.count();

    // Print summary statistics.
    cout << "Sent " << num_messages << " messages to " << dest_ip << "." << endl;
    if (!sourceIpString.empty()) {
        cout << "Using source IP: " << sourceIpString << endl;
    }
    cout << "Time taken: " << duration.count() << " seconds" << endl;
    // Use std::fixed and std::setprecision to format the messages per second output.
    cout << "Messages per second: " << fixed << setprecision(2) << messages_per_second << endl;

    return 0; // Indicate successful execution
}

