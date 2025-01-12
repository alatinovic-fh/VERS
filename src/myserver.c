#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <ldap.h>
#include "myqueue.h"

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024
#define PORT 6543
#define SIZE 9
#define SUBJ_SIZE 81
#define PASSWORD 255
#define SPOOL "../spool/"
// ThreadPOOL
#define THREAD_POOL_SIZE 50

///////////////////////////////////////////////////////////////////////////////

int abortRequested = 0;
int create_socket = -1;
int new_socket = -1;
int indx = 1;
char directory[BUF];

///////////////////////////////THREADS/////////////////////////////////////////
pthread_t thread_pool[THREAD_POOL_SIZE];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

node_t *head = NULL;
node_t *tail = NULL;

///////////////////////////////////////////////////////////////////////////////

void *clientCommunication(void *data);
void signalHandler(int sig);
void message(int *current_socket, char *buffer);
int create_dir(char *name);
void enqueue(int *client_socket);
int *dequeue();
void recv_message(int *current_socket, char *buffer);
void *thread_function(void *arg);
int login_verfication(char *userName, char *password);
void print_usage(char *programm_name);
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
   // create thread pool and save it in thread_pool array
   for (int i = 0; i < THREAD_POOL_SIZE; i++)
   {
      pthread_create(&thread_pool[i], NULL, thread_function, NULL);
   }

   socklen_t addrlen;
   struct sockaddr_in address, cliaddress;
   int reuseValue = 1;

   int port;

   ////////////////////////////////////////////////////////////////////////////
   // SIGNAL HANDLER
   // SIGINT (Interrup: ctrl+c)
   // https://man7.org/linux/man-pages/man2/signal.2.html
   if (signal(SIGINT, signalHandler) == SIG_ERR)
   {
      perror("signal can not be registered");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // CREATE A SOCKET
   // https://man7.org/linux/man-pages/man2/socket.2.html
   // https://man7.org/linux/man-pages/man7/ip.7.html
   // https://man7.org/linux/man-pages/man7/tcp.7.html
   // IPv4, TCP (connection oriented), IP (same as client)
   if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
      perror("Socket error"); // errno set by socket()
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // SET SOCKET OPTIONS
   // https://man7.org/linux/man-pages/man2/setsockopt.2.html
   // https://man7.org/linux/man-pages/man7/socket.7.html
   // socket, level, optname, optvalue, optlen
   if (setsockopt(create_socket,
                  SOL_SOCKET,
                  SO_REUSEADDR,
                  &reuseValue,
                  sizeof(reuseValue)) == -1)
   {
      perror("set socket options - reuseAddr");
      return EXIT_FAILURE;
   }

   if (setsockopt(create_socket,
                  SOL_SOCKET,
                  SO_REUSEPORT,
                  &reuseValue,
                  sizeof(reuseValue)) == -1)
   {
      perror("set socket options - reusePort");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // INIT ADDRESS
   // Attention: network byte order => big endian
   memset(&address, 0, sizeof(address));
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = INADDR_ANY;

   // if no arguments are given, use default port and default directory
   if (argc == 1)
   {
      address.sin_port = htons(PORT);
      sprintf(directory, "%s", SPOOL);
      printf("Using default port and directory\n");
   }
   // if 3 arguments are given, set the port and directory
   else if (argc == 3)
   {
      port = atoi(argv[1]);
      address.sin_port = htons(port);
      sprintf(directory, "../%s/", argv[2]);
   }
   // error
   else
   {
      print_usage(argv[0]);
      exit(EXIT_FAILURE);
   }

   ////////////////////////////////////////////////////////////////////////////
   // ASSIGN AN ADDRESS WITH PORT TO SOCKET
   if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1)
   {
      perror("bind error");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // ALLOW CONNECTION ESTABLISHING
   // Socket, Backlog (= count of waiting connections allowed)
   if (listen(create_socket, 5) == -1)
   {
      perror("listen error");
      return EXIT_FAILURE;
   }

   while (!abortRequested)
   {
      /////////////////////////////////////////////////////////////////////////
      // ignore errors here... because only information message
      // https://linux.die.net/man/3/printf
      printf("Waiting for connections...\n");

      /////////////////////////////////////////////////////////////////////////
      // ACCEPTS CONNECTION SETUP
      // blocking, might have an accept-error on ctrl+c
      addrlen = sizeof(struct sockaddr_in);
      if ((new_socket = accept(create_socket,
                               (struct sockaddr *)&cliaddress,
                               &addrlen)) == -1)
      {
         if (abortRequested)
         {
            perror("accept error after aborted");
         }
         else
         {
            perror("accept error");
         }
         break;
      }

      /////////////////////////////////////////////////////////////////////////
      // START CLIENT
      // ignore printf error handling
      printf("Client connected from %s:%d...\n",
             inet_ntoa(cliaddress.sin_addr),
             ntohs(cliaddress.sin_port));

      // our thread is saved on the heap
      int *pclient = (int *)malloc(10000 * sizeof(int));
      *pclient = new_socket;
      // to protect the queue mutex_lock is used
      pthread_mutex_lock(&mutex);
      enqueue(pclient);
      pthread_mutex_unlock(&mutex);
      /////////////////////////////////////////////////////////////////////
      new_socket = -1;
   }
   // frees the descriptor
   if (create_socket != -1)
   {
      if (shutdown(create_socket, SHUT_RDWR) == -1)
      {
         perror("shutdown create_socket");
      }
      if (close(create_socket) == -1)
      {
         perror("close create_socket");
      }
      create_socket = -1;
   }

   return EXIT_SUCCESS;
}
/////////////////////////////////////////////////////////////////////////////
void *thread_function(void *arg)
{
   // to keep the threads alive -> infinte loop
   while (true)
   {
      int *pclient;
      pthread_mutex_lock(&mutex);
      // check if anything is in the queue
      pclient = dequeue();
      pthread_mutex_unlock(&mutex);
      // if there is a client connecting, there is a connction to the server
      // communication is established
      if (pclient != NULL)
      {
         clientCommunication(pclient);
      }
   }
}
/////////////////////////////////////////////////////////////////////////////
void *clientCommunication(void *data)
{
   char buffer[BUF];
   char sender[SIZE];
   char receiver[SIZE];
   char subject[SUBJ_SIZE];
   char path[BUF];
   char messagePath[BUF];
   int *current_socket = (int *)data;
   char index[BUF] = {0};
   char msgNum[BUF];
   char userName[SIZE];
   char username[SIZE];
   char password[PASSWORD];
   DIR *d;
   struct dirent *dir;
   // int size;
   int sendMsg;

   ////////////////////////////////////////////////////////////////////////////
   mkdir(directory, 0777);

   ////////////////////////////////////////////////////////////////////////////
   // SEND welcome message
   strcpy(buffer, "Welcome to myserver!\r\nPlease enter your commands...\r\n");
   if (send(*current_socket, buffer, strlen(buffer), 0) == -1)
   {
      perror("send failed");
      return NULL;
   }
   // as long as the user doesn't enter LOGIN, ask for the right command
   do
   {
      bzero(buffer, BUF);
      recv_message(current_socket, buffer);
      int logInUser = strncmp("LOGIN", buffer, 5);

      if (logInUser == 0)
      {
         // ask for username and password
         bzero(buffer, BUF);
         strcpy(buffer, "Username: ");
         if (send(*current_socket, buffer, strlen(buffer), 0) == -1)
         {
            perror("send failed");
            return NULL;
         }
         recv_message(current_socket, buffer);
         strncpy(userName, buffer, SIZE - 1);

         bzero(buffer, BUF);

         strcpy(buffer, "Password: ");
         if (send(*current_socket, buffer, strlen(buffer), 0) == -1)
         {
            perror("send failed");
            return NULL;
         }
         bzero(buffer, BUF);
         recv_message(current_socket, buffer);
         strcpy(password, buffer);
         // verify username and password with ldap
         if (login_verfication(userName, password) == 0)
         {
            if (send(*current_socket, "OK", 3, 0) == -1)
            {
               perror("send answer failed");
               return NULL;
            }
            // save userName in username
            sprintf(username, "%s", userName);

            do
            {
               /////////////////////////////////////////////////////////////////////////
               bzero(buffer, BUF);
               message(current_socket, buffer);
               int sendMessage = strncmp("SEND", buffer, 4);
               int listMessage = strncmp("LIST", buffer, 4);
               int deleteMessage = strncmp("DEL", buffer, 3);
               int readMessage = strncmp("READ", buffer, 4);
               int quitMessage = strncmp("quit", buffer, 4);
               FILE *fp;

               if (sendMessage != 0 && listMessage != 0 && readMessage != 0 && deleteMessage != 0 && quitMessage != 0)
               {
                  printf("Unknown command: %s\n", buffer); // ignore error
                  if (send(*current_socket, "ERR", 4, 0) == -1)
                  {
                     perror("send answer failed");
                     return NULL;
                  }
               }
               strcpy(path, directory);

               /////////////////////SEND MESSAGE////////////////////////
               if (sendMessage == 0) // Check if the client sent the "SEND" command
               {
                  printf("SEND:\n");

                  // Save the sender's username
                  strncpy(sender, username, SIZE - 1); // Copy the sender's username into the sender buffer, leaving space for the null terminator
                  sender[SIZE] = '\0'; // Ensure the sender buffer is null-terminated
                  printf("Sender: %s\n", sender); // Debug output to confirm the sender's name

                  // Receive and save the receiver's username
                  bzero(buffer, BUF); // Clear the buffer to prepare for new data
                  bzero(receiver, SIZE); // Clear the receiver buffer
                  message(current_socket, buffer); // Receive the receiver's username from the client
                  strncpy(receiver, buffer, SIZE - 1); // Copy the receiver's username into the receiver buffer, leaving space for the null terminator
                  receiver[SIZE] = '\0'; // Ensure the receiver buffer is null-terminated
                  printf("Receiver: %s\n", receiver); // Debug output to confirm the receiver's name

                  // Create a directory for the receiver if it doesn't already exist
                  strcat(path, receiver); // Append the receiver's name to the base path
                  create_dir(path); // Create the directory for the receiver

                  // Prepare the path for the message file
                  strcpy(messagePath, path); // Start with the receiver's directory path
                  strcat(messagePath, "/"); // Add a trailing slash to the path

                  // Open or update the index file to get the current message index
                  strcat(path, "/index.txt"); // Append the index file name to the path
                  if (access(path, F_OK) == 0) // Check if the index file exists
                  {
                     pthread_mutex_lock(&mutex); // Lock the mutex to ensure thread-safe access
                     fp = fopen(path, "r"); // Open the index file for reading
                     if (fp == NULL) // Handle errors while opening the file
                     {
                           if (send(*current_socket, "ERR", 4, 0) == -1) // Notify the client of the error
                           {
                              perror("send answer failed");
                              return NULL;
                           }
                           perror("Failed to open file: ");
                           break;
                     }
                     fgets(index, BUF, fp); // Read the current message index from the file
                     fclose(fp); // Close the index file
                  }

                  indx = atoi(index); // Convert the index from a string to an integer
                  indx++; // Increment the index for the new message

                  // Write the updated index back to the index file
                  fp = fopen(path, "w"); // Open the index file for writing
                  if (fp == NULL) // Handle errors while opening the file
                  {
                     if (send(*current_socket, "ERR", 4, 0) == -1) // Notify the client of the error
                     {
                           perror("send answer failed");
                           return NULL;
                     }
                     perror("Failed to open file: ");
                     break;
                  }
                  fprintf(fp, "%d", indx); // Write the updated index to the file
                  fclose(fp); // Close the index file
                  pthread_mutex_unlock(&mutex); // Unlock the mutex

                  // Prepare the full path for the new message file
                  sprintf(index, "%d", indx); // Convert the updated index back to a string
                  strcat(messagePath, index); // Append the index to the message path
                  strcat(messagePath, ".txt"); // Add the .txt extension to the message path

                  // Receive the subject of the message
                  bzero(buffer, BUF); // Clear the buffer
                  bzero(subject, SUBJ_SIZE); // Clear the subject buffer
                  message(current_socket, buffer); // Receive the subject from the client
                  strncpy(subject, buffer, SUBJ_SIZE - 1); // Copy the subject into the subject buffer
                  subject[SUBJ_SIZE] = '\0'; // Ensure the subject buffer is null-terminated
                  printf("Subject: %s\n", subject); // Debug output to confirm the subject

                  // Open the new message file for writing
                  fp = fopen(messagePath, "a"); // Open the message file in append mode
                  if (fp == NULL) // Handle errors while opening the file
                  {
                     if (send(*current_socket, "ERR", 4, 0) == -1) // Notify the client of the error
                     {
                           perror("send answer failed");
                           return NULL;
                     }
                     perror("Failed to open file: ");
                     break;
                  }

                  // Write the sender, receiver, and subject to the message file
                  fprintf(fp, "%s\n", sender); // Write the sender's name
                  fprintf(fp, "%s\n", receiver); // Write the receiver's name
                  fprintf(fp, "%s\n", subject); // Write the subject

                  // Receive the message body from the client
                  while (1)
                  {
                     bzero(buffer, BUF); // Clear the buffer for each new line
                     message(current_socket, buffer); // Receive the next line of the message
                     if (strncmp(".", buffer, 1) == 0) // Check for the end-of-message indicator (".")
                     {
                           printf("End of message!\n"); // Debug output
                           break; // Exit the loop when the message ends
                     }

                     printf("Message: %s\n", buffer); // Debug output to show the message content
                     fprintf(fp, "%s\n", buffer); // Write the message line to the file
                  }

                  fclose(fp); // Close the message file
                  if (send(*current_socket, "OK", 3, 0) == -1) // Notify the client of success
                  {
                     perror("send answer failed");
                     return NULL;
                  }
               }

               ///////////////////////LIST MESSAGES////////////////////////
               if (listMessage == 0) { // Check if the client sent the "LIST" command
                  printf("LIST: \n");

                  // Prepare the user's directory path
                  strcpy(receiver, username); // Set the receiver to the logged-in user's username
                  strcpy(path, directory); // Start with the base directory
                  strcat(path, receiver); // Append the username to form the user's message directory path

                  // Check if the user's directory exists
                  if (access(path, F_OK) == -1) { // If the directory does not exist
                     printf("User doesn't exist! Creating directory: %s\n", path);
                     create_dir(path); // Create the directory for the user
                  }

                  // Open the user's directory
                  d = opendir(path); // Attempt to open the directory
                  if (!d) { // If opening the directory fails
                     perror("Failed to open directory"); // Print an error message
                     if (send(*current_socket, "ERR", 4, 0) == -1) { // Notify the client about the error
                           perror("send answer failed");
                     }
                     continue; // Skip further processing for this request
                  }

                  // Initialize the message counter
                  int counter = 0;

                  // Iterate over the files in the user's directory
                  while ((dir = readdir(d)) != NULL) { // Read each directory entry
                     // Check if the entry is a regular file and not "index.txt"
                     if (dir->d_type == DT_REG && strcmp(dir->d_name, "index.txt") != 0) {
                           // Message file found
                           printf("Message found: %s\n", dir->d_name);

                           // Create the full path to the message file
                           snprintf(messagePath, BUF, "%s/%s", path, dir->d_name);

                           // Open the message file for reading
                           fp = fopen(messagePath, "r");
                           if (!fp) { // Handle error opening the file
                              perror("Failed to open message file");
                              continue; // Skip this file and proceed with the next
                           }

                           // Read the subject line from the message file
                           for (int i = 0; i < 3; i++) { // Skip to the third line (subject line)
                              fgets(subject, SUBJ_SIZE, fp);
                           }
                           fclose(fp); // Close the file after reading the subject

                           // Prepare the message details to send to the client
                           snprintf(buffer, BUF, "Subject: %s", subject);
                           printf("Sending: %s\n", buffer); // Debug output of the message being sent

                           // Send the message details to the client
                           if (send(*current_socket, buffer, strlen(buffer), 0) == -1) {
                              perror("send failed"); // Handle errors in sending the message
                           }
                           counter++; // Increment the message counter
                     }
                  }
                  closedir(d); // Close the directory after reading all entries

                  // If no messages are found, notify the client
                  if (counter == 0) {
                     strcpy(buffer, "You have 0 messages.\r\n"); // Prepare the message
                     if (send(*current_socket, buffer, strlen(buffer), 0) == -1) { // Send it to the client
                           perror("send failed");
                     }
                  }

                  // Send confirmation to the client
                  if (send(*current_socket, "OK", 3, 0) == -1) { // Notify the client that the operation was successful
                     perror("send answer failed");
                  }
               }

               //////////////////////////////////////READ MESSAGE///////////////////////
               if (readMessage == 0)
               {
                  printf("READ:\n");
                  // Save user name
                  bzero(buffer, BUF);
                  bzero(receiver, SIZE);

                  strncpy(receiver, username, SIZE - 1);
                  receiver[SIZE] = '\0';

                  // Create a path for the user
                  strcpy(path, directory);
                  strcat(path, receiver);

                  // Check if the user exists
                  if (access(path, F_OK) == -1)
                  {
                     printf("User doesn't exist!\n");
                     if (send(*current_socket, "ERR", 4, 0) == -1)
                     {
                           perror("send answer failed");
                           return NULL;
                     }
                     continue;
                  }

                  // Read the number of the message
                  bzero(buffer, BUF);
                  bzero(msgNum, BUF);
                  message(current_socket, msgNum);

                  // Create path to the message
                  strcpy(messagePath, path);
                  strcat(messagePath, "/");
                  strcat(messagePath, msgNum);
                  strcat(messagePath, ".txt");

                  // Open the message file
                  FILE *fp = fopen(messagePath, "r");
                  if (fp == NULL)
                  {
                     perror("Failed to open file");
                     if (send(*current_socket, "ERR", 4, 0) == -1)
                     {
                           perror("send answer failed");
                     }
                     return NULL;
                  }

                  // Build the complete file content
                  char *fileContent = NULL;
                  size_t totalSize = 0;
                  while (fgets(buffer, BUF, fp) != NULL)
                  {
                     size_t lineLength = strlen(buffer);

                     // Expand the fileContent buffer
                     char *newContent = realloc(fileContent, totalSize + lineLength + 1);
                     if (!newContent)
                     {
                           perror("Memory allocation failed");
                           free(fileContent);
                           fclose(fp);
                           if (send(*current_socket, "ERR", 4, 0) == -1)
                           {
                              perror("send answer failed");
                           }
                           return NULL;
                     }

                     fileContent = newContent;
                     strcpy(fileContent + totalSize, buffer); // Append line to the content
                     totalSize += lineLength;
                  }

                  fclose(fp);

                  // Send the entire content at once
                  if (fileContent)
                  {
                     fileContent[totalSize] = '\0'; // Null-terminate
                     if (send(*current_socket, fileContent, totalSize, 0) == -1)
                     {
                           perror("send failed");
                           free(fileContent);
                           return NULL;
                     }
                     printf("Sent content:\n%s", fileContent);
                     free(fileContent);
                  }

                  // Send confirmation
                  if (send(*current_socket, "OK", 3, 0) == -1)
                  {
                     perror("send answer failed");
                     return NULL;
                  }
               }



               ///////////////////////////DELETE MESSAGE//////////////////////////////////
               if (deleteMessage == 0)
               {
                  printf("DELETE\n");
                  bzero(buffer, BUF);
                  bzero(receiver, SIZE);
                  // save username
                  strncpy(receiver, username, SIZE - 1);
                  receiver[SIZE] = '\0';

                  // create a path for user
                  strcpy(path, directory);
                  strcat(path, receiver);

                  // check if the user exists
                  if (access(path, F_OK) == -1)
                  {
                     printf("User doesn't exist!\n");
                     if (send(*current_socket, "ERR", 4, 0) == -1)
                     {
                        perror("send answer failed");
                        return NULL;
                     }
                     continue;
                  }

                  // read the number of the message
                  bzero(buffer, BUF);
                  bzero(msgNum, BUF);
                  message(current_socket, msgNum);

                  // create path to the message
                  strcpy(messagePath, path);
                  strcat(messagePath, "/");
                  strcat(messagePath, msgNum);
                  strcat(messagePath, ".txt");

                  // open user directory
                  d = opendir(path);
                  // if directory can be opened, find the file and delete it
                  if (d)
                  {
                     strcpy(index, msgNum);
                     strcat(index, ".txt");
                     while ((dir = readdir(d)) != NULL)
                     {
                        if (dir->d_type == DT_REG && strcmp(dir->d_name, index) == 0) // regular file
                        {
                           if (remove(messagePath) == 0)
                           {
                              printf("File deleted successfully.\n");
                              if (send(*current_socket, "OK", 3, 0) == -1)
                              {
                                 perror("send answer failed");
                                 return NULL;
                              }
                              break;
                           }
                           else
                           {
                              printf("Unable to delete the file.\n");
                              if (send(*current_socket, "ERR", 4, 0) == -1)
                              {
                                 perror("send answer failed");
                                 return NULL;
                              }
                              break;
                           }
                        }
                        else if (dir->d_type == DT_REG && strcmp(dir->d_name, index) != 0)
                        {
                           printf("No such file.\n");
                           if (send(*current_socket, "ERR", 4, 0) == -1)
                           {
                              perror("send answer failed");
                              return NULL;
                           }
                        }
                     }
                     closedir(d);
                  }
               }
            } while (strcmp(buffer, "quit") != 0 && !abortRequested);
         }
         else
         {
            // wrong credentials
            strcpy(buffer, "Wrong Password or Username\r\n");
            printf("%s\n", buffer);
            if (send(*current_socket, "ERR", 4, 0) == -1)
            {
               perror("send answer failed");
               return NULL;
            }
         }
      }
      else
      {
         if (strncmp(buffer, "quit", 4) != 0)
         {
            // if command not quit or LOGIN
            bzero(buffer, BUF);
            strcpy(buffer, "Command not recoginzed! Try again!\r\n");
            printf("%s", buffer);
            if (send(*current_socket, buffer, strlen(buffer), 0) == -1)
            {
               perror("send failed");
               return NULL;
            }
         }
      }
   } while (strcmp(buffer, "quit") != 0 && !abortRequested);

   // closes/frees the descriptor if not already
   if (*current_socket != -1)
   {
      if (shutdown(*current_socket, SHUT_RDWR) == -1)
      {
         perror("shutdown new_socket");
      }
      if (close(*current_socket) == -1)
      {
         perror("close new_socket");
      }
      *current_socket = -1;
   }

   return NULL;
}
//////////////////////////////////////////////////////////////////////////////
void signalHandler(int sig)
{
   if (sig == SIGINT)
   {
      printf("abort Requested... "); // ignore error
      abortRequested = 1;
      /////////////////////////////////////////////////////////////////////////
      // With shutdown() one can initiate normal TCP close sequence ignoring
      // the reference count.
      // https://beej.us/guide/bgnet/html/#close-and-shutdownget-outta-my-face
      // https://linux.die.net/man/3/shutdown
      if (new_socket != -1)
      {
         if (shutdown(new_socket, SHUT_RDWR) == -1)
         {
            perror("shutdown new_socket");
         }
         if (close(new_socket) == -1)
         {
            perror("close new_socket");
         }
         new_socket = -1;
      }

      if (create_socket != -1)
      {
         if (shutdown(create_socket, SHUT_RDWR) == -1)
         {
            perror("shutdown create_socket");
         }
         if (close(create_socket) == -1)
         {
            perror("close create_socket");
         }
         create_socket = -1;
      }
   }
   else
   {
      exit(sig);
   }
}
//////////////////////////////////////////////////////////////////////////////
void message(int *current_socket, char *buffer)
{
   // send and receive messages
   int size;
   if (send(*current_socket, "OK", 3, 0) == -1)
   {
      perror("send answer failed");
      return;
   }
   size = recv(*current_socket, buffer, BUF - 1, 0);
   if (size == -1)
   {
      if (abortRequested)
      {
         perror("recv error after aborted");
      }
      else
      {
         perror("recv error");
      }
      exit(1);
   }

   if (size == 0)
   {
      printf("Client closed remote socket\n"); // ignore error
      exit(1);
   }

   // remove ugly debug message, because of the sent newline of client
   if (buffer[size - 2] == '\r' && buffer[size - 1] == '\n')
   {
      size -= 2;
   }
   else if (buffer[size - 1] == '\n')
   {
      --size;
   }

   buffer[size] = '\0';
}
//////////////////////////////////////////////////////////////////////////////
int create_dir(char *name)
{
   // create a directory if it doesn't exist
   int rc;
   rc = mkdir(name, S_IRWXU);

   if (rc != 0 && errno != EEXIST)
   {
      perror("mkdir");
      exit(1);
   }
   if (rc != 0 && errno == EEXIST)
   {
      printf("%s already exists.\n", name);
   }
   return 0;
}
//////////////////////////////////////////////////////////////////////////////
void enqueue(int *client_socket)
{
   // create node in a linked list and add new node in the queue
   node_t *newnode = (node_t *)malloc(100000 * sizeof(node_t));
   newnode->client_socket = client_socket;
   newnode->next = NULL;
   if (tail == NULL)
   {
      head = newnode;
   }
   else
   {
      tail->next = newnode;
   }
   tail = newnode;
}
//////////////////////////////////////////////////////////////////////////////
int *dequeue()
{
   // free queue
   if (head == NULL)
   {
      return NULL;
   }
   else
   {
      int *result = head->client_socket;
      node_t *temp = head;
      head = head->next;
      if (head == NULL)
      {
         tail = NULL;
      }
      free(temp);
      return result;
   }
}
//////////////////////////////////////////////////////////////////////////////
void recv_message(int *current_socket, char *buffer)
{
   // receive message
   int size;
   size = recv(*current_socket, buffer, BUF - 1, 0);
   if (size == -1)
   {
      if (abortRequested)
      {
         perror("recv error after aborted");
      }
      else
      {
         perror("recv error");
      }
      exit(1);
   }

   if (size == 0)
   {
      printf("Client closed remote socket\n"); // ignore error
      exit(1);
   }

   // remove ugly debug message, because of the sent newline of client
   if (buffer[size - 2] == '\r' && buffer[size - 1] == '\n')
   {
      size -= 2;
   }
   else if (buffer[size - 1] == '\n')
   {
      --size;
   }

   buffer[size] = '\0';
}
//////////////////////////////////////////////////////////////////////////////
int login_verfication(char *username, char *password)
{
   const char *ldapUri = "ldap://ldap.technikum-wien.at:389";
   const int ldapVersion = LDAP_VERSION3;

   // read username (bash: export ldapuser=<yourUsername>)
   char ldapBindUser[256];
   char rawLdapUser[128]; // username

   // if the username exist, save it to rawLdapUser
   if (username != NULL)
   {
      strcpy(rawLdapUser, username);
      sprintf(ldapBindUser, "uid=%s,ou=people,dc=technikum-wien,dc=at", rawLdapUser);
      printf("user set to: %s\n", ldapBindUser);
   }
   else
   {
      const char *rawLdapUserEnv = getenv("ldapuser");
      if (rawLdapUserEnv == NULL)
      {
         printf("(user not found... set to empty string)\n");
         strcpy(ldapBindUser, "");
      }
      else
      {
         sprintf(ldapBindUser, "uid=%s,ou=people,dc=technikum-wien,dc=at", rawLdapUserEnv);
         printf("user based on environment variable ldapuser set to: %s\n", ldapBindUser);
      }
   }

   // read password (bash: export ldappw=<yourPW>)
   char ldapBindPassword[256]; // password
   // if the password is not empty, save it to ldapBindPassword
   if (password != NULL)
   {
      strcpy(ldapBindPassword, password);
      // if (ldapBindPassword)
      //    printf("pw taken over from commandline\n");
   }
   else
   {
      const char *ldapBindPasswordEnv = getenv("ldappw");
      if (ldapBindPasswordEnv == NULL)
      {
         strcpy(ldapBindPassword, "");
         printf("(pw not found... set to empty string)\n");
      }
      else
      {
         strcpy(ldapBindPassword, ldapBindPasswordEnv);
         printf("pw taken over from environment variable ldappw\n");
      }
   }

   // general
   int rc = 0; // return code

   ////////////////////////////////////////////////////////////////////////////
   // setup LDAP connection
   // https://linux.die.net/man/3/ldap_initialize
   LDAP *ldapHandle;
   rc = ldap_initialize(&ldapHandle, ldapUri);
   if (rc != LDAP_SUCCESS)
   {
      fprintf(stderr, "ldap_init failed\n");
      return EXIT_FAILURE;
   }
   printf("connected to LDAP server %s\n", ldapUri);

   ////////////////////////////////////////////////////////////////////////////
   // set verison options
   // https://linux.die.net/man/3/ldap_set_option
   rc = ldap_set_option(
       ldapHandle,
       LDAP_OPT_PROTOCOL_VERSION, // OPTION
       &ldapVersion);             // IN-Value
   if (rc != LDAP_OPT_SUCCESS)
   {
      // https://www.openldap.org/software/man.cgi?query=ldap_err2string&sektion=3&apropos=0&manpath=OpenLDAP+2.4-Release
      fprintf(stderr, "ldap_set_option(PROTOCOL_VERSION): %s\n", ldap_err2string(rc));
      ldap_unbind_ext_s(ldapHandle, NULL, NULL);
      return EXIT_FAILURE;
   }

   rc = ldap_start_tls_s(
       ldapHandle,
       NULL,
       NULL);
   if (rc != LDAP_SUCCESS)
   {
      fprintf(stderr, "ldap_start_tls_s(): %s\n", ldap_err2string(rc));
      ldap_unbind_ext_s(ldapHandle, NULL, NULL);
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // bind credentials

   BerValue bindCredentials;
   bindCredentials.bv_val = (char *)ldapBindPassword;
   bindCredentials.bv_len = strlen(ldapBindPassword);
   BerValue *servercredp; // server's credentials
   rc = ldap_sasl_bind_s(
       ldapHandle,
       ldapBindUser,
       LDAP_SASL_SIMPLE,
       &bindCredentials,
       NULL,
       NULL,
       &servercredp);
   if (rc != LDAP_SUCCESS)
   {
      fprintf(stderr, "LDAP bind error: %s\n", ldap_err2string(rc));
      ldap_unbind_ext_s(ldapHandle, NULL, NULL);
      return EXIT_FAILURE;
   }
   return 0;
}
//////////////////////////////////////////////////////////////////////////////
void print_usage(char *programm_name)
{
   printf("Usage: %s <port> <mail-spool-directoryname>\n", programm_name);
}
