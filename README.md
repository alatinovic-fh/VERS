# Client-Server Architecture Description

## **Client-Server Architecture**

### **Overview**
- The architecture follows a client-server model with:
  - **Client (`myclient.c`)**: A command-line application enabling users to connect to the server, send commands, and receive responses.
  - **Server (`myserver.c`)**: A multi-threaded server that handles multiple client connections using a thread pool and provides functionalities like login authentication, message handling, and directory operations.

### **Communication**
- Utilizes **IPv4** and **TCP** for reliable connection-oriented communication.
- Both client and server operate on a specific port (`6543` by default) but allow custom configuration.

---

## **Used Technologies and Libraries**

### **Standard Libraries**
- **Socket programming**: `sys/socket.h`, `netinet/in.h`, `arpa/inet.h`, `unistd.h` for networking.
- **File and directory operations**: `dirent.h`, `sys/stat.h`, `stdio.h`.
- **LDAP integration**: `ldap.h` for user authentication.

### **Custom Modules**
- `mypw`: Handles secure password input without echoing on the terminal.
- `myqueue`: Implements a thread-safe queue for managing client socket descriptors.

### **Concurrency**
- **POSIX Threads (pthreads)**:
  - A thread pool of 50 threads ensures scalability.
  - Mutex locks protect shared resources like the queue.

---

## **Development Strategy and Protocol Adaptations**

### **Development Approach**
- **Modularity**: Separate responsibilities for authentication (`mypw`), message queuing (`myqueue`), and server logic.
- **Thread Safety**: Use of mutexes ensures synchronization in multi-threaded operations like queue management.
- **Error Handling**: Extensive use of error-checking mechanisms for robust operations.

### **Protocol Adaptations**
- The server uses a custom protocol to process client commands like `LOGIN`, `SEND`, `LIST`, `READ`, and `DEL`. Each command follows a well-defined request-response pattern.
- For large messages, the server employs iterative reading and writing to manage memory efficiently.

---

## **Synchronization Methods**

### **Thread Pool**
- Each thread continuously checks for new clients in the queue, allowing efficient resource allocation.

### **Queue Management**
- Enqueue and dequeue operations for client sockets are synchronized using mutex locks.

### **Critical Sections**
- Shared resources like the index file and message directories are accessed within mutex-protected blocks.

---

## **Handling of Large Messages**

### **Chunked Communication**
- The server reads and writes data in fixed-size buffers (`BUF=1024`) to handle messages larger than memory.
- For file-based operations, messages are streamed to and from files incrementally.

### **Dynamic Memory Management**
- The `READ` command dynamically reallocates memory to construct the complete message before sending it to the client.

### **Error Handling**
- If a client disconnects during a transaction, the server safely releases resources and logs errors.

---

This document outlines the architecture, technologies, and strategies employed in the system, emphasizing scalability, security, and robustness.
