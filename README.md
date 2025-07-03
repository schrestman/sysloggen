# sysloggen

`sysloggen` is a high-speed, multithreaded syslog message generator for Linux and macOS. It is designed for performance testing of syslog servers and log processing pipelines.

## Features

-   **High Performance:** Multithreaded architecture to generate and send messages at a very high rate.
-   **Customizable Message Content:** Use your own message templates from a file.
-   **Customizable Hostnames:** Use a list of hostnames to simulate a variety of sources.
-   **Source IP Spoofing:** Bind to a specific source IP address to test network routing and source-based filtering.
-   **RFC 3164 Compliance:** Generates syslog messages in the standard RFC 3164 format.
-   **Performance Metrics:** Reports the total time taken and the sending rate in messages per second.

## Compilation

To compile `sysloggen`, you need a C++ compiler that supports C++20 and the pthreads library.

```bash
g++ -std=c++20 -o sysloggen sysloggen.cpp -lpthread
```

## Usage

```
./sysloggen <Destination_IP> <Port> <NumMessages> <NumThreads> [options]
```

### Parameters

-   `<Destination_IP>`: The IP address of the syslog receiver.
-   `<Port>`: The port number of the syslog receiver (e.g., 514).
-   `<NumMessages>`: The total number of messages to send.
-   `<NumThreads>`: The number of concurrent threads to use for sending messages.

### Options

-   `-s <Source_IP>`: (Optional) Specifies the source IP address for the outgoing packets. If not provided, the OS will choose the source IP.
-   `-f <message_file>`: (Optional) Path to a file containing messages, with one message per line. If not provided, random messages will be generated.
-   `-h <host_file>`: (Optional) Path to a file containing hostnames, with one hostname per line. If not provided, the default hostname "myhost" will be used.

### Examples

**Basic Usage**

Send 1,000,000 messages to `192.168.1.100` on port `514` using `4` threads:

```bash
./sysloggen 192.168.1.100 514 1000000 4
```

**Using a Message File**

Send messages from a file named `messages.txt`:

```bash
./sysloggen 192.168.1.100 514 1000000 4 -f messages.txt
```

**Using a Hostname File and Source IP**

Send messages from `messages.txt`, use hostnames from `hosts.txt`, and set the source IP to `10.0.0.5`:

```bash
./sysloggen 192.168.1.100 514 1000000 4 -f messages.txt -h hosts.txt -s 10.0.0.5
```

## How It Works

`sysloggen` creates a specified number of worker threads. Each thread is responsible for generating and sending a portion of the total messages.

-   **Message Generation:** If a message file is provided with the `-f` option, a random line from the file is chosen for each message. Otherwise, a random alphanumeric string is generated.
-   **Hostname Generation:** If a host file is provided with the `-h` option, a random hostname from the file is chosen for each message. Otherwise, the default hostname "myhost" is used.
-   **Syslog Formatting:** Each message is formatted according to RFC 3164, including a priority value, timestamp, hostname, and message content.
-   **Networking:** Messages are sent over UDP sockets. For maximum speed, the tool does not wait for a response or check for delivery errors, which is a common practice for high-volume UDP-based tools.

## License

This project is licensed under the MIT License.
