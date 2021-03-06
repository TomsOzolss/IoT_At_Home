#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <getopt.h>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include "RF24/RF24.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sstream>

#include "./utils/sysl.hpp"
#include "./utils/iot_types.h"
#include "./utils/threading_functions.h"

using namespace std;

#define DEBUG 0
#define NUM_THREADS 20
#define CMD_LINE_MAX 100

Logger logProcess("IoT_GW");

//RF24 radio("/dev/spidev0.0",8000000 , 25);  //spi device, speed and CSN,only CSN is NEEDED in RPI
RF24 radio(RPI_V2_GPIO_P1_22, RPI_V2_GPIO_P1_24, BCM2835_SPI_SPEED_8MHZ);
const int role_pin = 7;
const uint64_t pipes[2] = { 0xF0F0F0F0D2LL, 0xF0F0F0F0E1LL };

void log(char print_to_stdout, const char *message, ...)
{
    #if (DEBUG == 1)
       logProcess.log(print_to_stdout, message);
    #endif
}

void logError(char print_to_stdout, const char *message, ...)
{
    #if (DEBUG == 1)
       logProcess.logError(print_to_stdout, message);
    #endif
}

void setup(void){
    //Prepare the radio module
    log(1, "INFO: Preparing interface");
    radio.begin();
    radio.setPayloadSize(32);
    radio.setChannel(0x4c);
    radio.setPALevel(RF24_PA_MAX);

    radio.setDataRate(RF24_250KBPS);
    radio.setAutoAck(true);
    radio.setRetries(15, 15);
    radio.openWritingPipe(pipes[1]);
    radio.openReadingPipe(1,pipes[0]);

    radio.startListening();
    radio.printDetails();
    log(1, "INFO: Finished interface. Radio Data Available: %s", radio.available()?"Yes":"No");
}

static pthread_mutex_t cs_mutex =  PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

void* sendDataToCloud(void *cmd)
{
    struct cmdToThread sNew;

    memcpy(&sNew, cmd, sizeof(cmdToThread));

//--------------------Some workaround on passing data from python------------------
//Another possibility is to: 
//1. utilize some IPC
//2. Study python executor for C
    log(1, "INFO: Command Type:%i\r\n", sNew.type);

    if(sNew.type == REGISTER_NEW_DEVICE_MSG)
    {
        char temp[1024];
        const char strToSearch[] = "Device Handshake ID=";

        sprintf(temp, "%s > temp%i.dat", sNew.cmd, sNew.currentThreadId);

        system(temp);

        sprintf(temp, "cat temp%i.dat | grep '%s' > regTemp%i.dat", sNew.currentThreadId, strToSearch, sNew.currentThreadId);
        system(temp);

        sprintf(temp, "regTemp%i.dat", sNew.currentThreadId);

        FILE *regID = fopen(temp, "r");

        if( regID == NULL )
        {
            //perror("Error while opening the file.\n");
            logError(1, "ERROR: Error while opening the file.");
            exit(EXIT_FAILURE);
        }

        //Assuming that file should be one liner
        char tempLine[512];
        if ( fgets (tempLine, sizeof(tempLine)-1, regID) != NULL )
        {
            if (strstr(tempLine, strToSearch) != NULL)
            {
                char * pch = tempLine;
                for (unsigned int i=0; i<strlen(tempLine); i++, pch++)
                {
                    if (tempLine[i] == '=')
                    {
                        char idNumber[10];
                        sprintf(idNumber, "%s", pch+1);

                        int registrationId = atoi(idNumber);
                        cout << "idNumber: |" << registrationId << "|\n";

                        fclose(regID);

                        char a[10];
                        sprintf(a, "%03i", registrationId);
                        radio.stopListening();
                        int transmitted = 0, retryCount = 0;
                        #define MAX_RETRY_COUNT 30

                        pthread_mutex_lock( &cs_mutex );
                        while(1)
                        {
                            transmitted = radio.write(a, sizeof(a));
                            printf ("!!!!!!!!!!!!!!!transmitted:%i\r\n", transmitted);

                            if(transmitted || (++retryCount > MAX_RETRY_COUNT))
                            {////TODO: TESTING: Add guard blocker, e.g. max retry count
                                if (retryCount > MAX_RETRY_COUNT)
                                    logError(1, "Registration ID (%i) broadcast - failed...MAX_RETRY_COUNT reached...Dropping", registrationId);
                                break;
                            }

                            logError(1, "Registration ID (%i) broadcast - failed...Retrying", registrationId);
                            delayMicroseconds(500);
                            radio.stopListening();
                        }
                        pthread_mutex_unlock( &cs_mutex );

                        radio.startListening();
                        delayMicroseconds(500);

                        if(transmitted == 1)
                            log(1, "INFO: Registration ID (%i) broadcast - ok.", registrationId);
                        else
                            if(transmitted == 2)
                                logError(1, "Registration ID (%i) broadcast - HARDWARE Error...Dropping", registrationId);

                        break;
                    }
                }
            }
        }

        sprintf(temp, "rm temp%i.dat regTemp%i.dat", sNew.currentThreadId, sNew.currentThreadId);
        system(temp);
//--------------------------------------------------------------------------------
    }
    else
    {
        system(sNew.cmd);
    }

    log(1, "INFO: Exiting the Thread Nr.%i\n\n", sNew.currentThreadId);

    return NULL;
}

int main( int argc, char ** argv)
{
    //TODO: Replace this parameter with configuration parameter
    int32_t GW_info_send_timeout = 5; //5 seconds
    time_t base_time;

    log(1, "INFO: Program started");

    setup();

    pthread_t threads[NUM_THREADS];
    int nextThreadId = 0;

    radio.startListening();

    base_time = time(nullptr);

    while(1)
    {
        if(time(nullptr) >= base_time+GW_info_send_timeout)
        {
            base_time = time(nullptr);

            ostringstream oss;
            oss << GW_info_send_timeout << " seconds passed\r\n";
            cout << oss.str();

            pthread_mutex_lock( &cs_mutex );
#if (DEBUG == 1)
            printf("Switched to listening!\r\n");
#endif
            radio.startListening();
            pthread_mutex_unlock( &cs_mutex );
        }

        delayMicroseconds(100000);

        if(radio.available())
        {
            char abc[100];

            pthread_mutex_lock( &cs_mutex );
            radio.read( abc, sizeof(abc) );
            pthread_mutex_unlock( &cs_mutex );
            log(1, "INFO: Radio Data Available:%s\n", abc);

#if (DEBUG == 1)
            printf(abc, "?_v1_346");
#endif

            if (abc[0] != '?')
            {
                if (abc[0] != '!')
                {
                    ///////////////////////////////////////////////////////////////////////////////////////////
                    ////////////////////////         Sent to IoT Platform                //////////////////////
                    ///////////////////////////////////////////////////////////////////////////////////////////
                    struct cmdToThread s;

                    snprintf(s.cmd, sizeof(s.cmd), "./gw.py %s", abc);

                    if (nextThreadId >= NUM_THREADS)
                        nextThreadId = 0;

                    s.currentThreadId = nextThreadId;
                    s.type = SENSOR_DATA_MSG;//data

                    int err = pthread_create(&(threads[nextThreadId]), NULL, &sendDataToCloud, (void*)&s);
                    pthread_detach(threads[nextThreadId]);

                    if (err != 0)
                        logError(1, "ERROR: Can't create thread :[%s]", strerror(err));
                    else
                        log(1, "INFO: Thread Nr.%i created!\n", s.currentThreadId);

                    nextThreadId++;
                }
                else
                {
                    //TODO: Process data Give-out for particular sensor node.
                }
            }
            else
            {
                    ///////////////////////////////////////////////////////////////////////////////////////////
                    ////////////////////////      Sent to IoT Platform for registration  //////////////////////
                    ///////////////////////////////////////////////////////////////////////////////////////////
                    struct cmdToThread sReg;

                    cout << "Registration Request Received:" << abc << "\n";

                    snprintf(sReg.cmd, sizeof(sReg.cmd), "./gw.py %s", abc);

                    if (nextThreadId >= NUM_THREADS)
                      nextThreadId = 0;

                    sReg.currentThreadId = nextThreadId;
                    sReg.type = REGISTER_NEW_DEVICE_MSG;//reg

                    int err = pthread_create(&(threads[nextThreadId]), NULL, &sendDataToCloud, (void*)&sReg);
                    pthread_detach(threads[nextThreadId]);

                    if (err != 0)
                        logError(1, "ERROR: Can't create thread :[%s]", strerror(err));
                    else
                        log(1, "INFO: Thread Nr.%i created!\n", sReg.currentThreadId);

                    nextThreadId++;
            }
        }//if(radio.available())

        pthread_mutex_lock( &cs_mutex );
        radio.startListening();
        delayMicroseconds(20*10);
        pthread_mutex_unlock( &cs_mutex );
    }//while(1)
}
