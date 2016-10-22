#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <contiki.h>
#include <contiki-net.h>

#include "dev/leds.h"
#include "lib/pic32_uart.h"

#include "letmecreate/core/debug.h"
#include "letmecreate/core/network.h"
#include "letmecreate/core/i2c.h"

#include "dev/flash.h"
#include "cfs-coffee-arch.h"

// --- Network stuff --- //

#define SERVER_IP_ADDR "2001:1418:100::1"
#define SERVER_PORT 3000


#define INPUTBUFSIZE 400
static uint8_t inputbuf[INPUTBUFSIZE];

#define OUTPUTBUFSIZE 400
static uint8_t outputbuf[OUTPUTBUFSIZE];


static tcp_socket_event_t last_tcp_event;
static process_event_t tcp_event;
static process_event_t tcp_data_received;

// --- End network stuff --- //


typedef enum {I2C, SPI, GPIO, UART, CORE} bus_type;
typedef enum {INIT, RELEASE, WRITE, READ} cmd_type;


/*---------------------------------------------------------------------------*/
PROCESS(main_process, "Main process");
AUTOSTART_PROCESSES(&main_process);

/*---------------------------------------------------------------------------*/
typedef struct linked_list { 
	char* data;
	struct linked_list * next; 
} linked_list_t;


/*---------------------------------------------------------------------------*/
// e.g. in:  (a -> b -> 0) (c -> 0)
//      out: a -> b -> c -> 0
void add_at_the_end(linked_list_t *list, linked_list_t *new_item) {
	
	if (list) {
		while (list->next)
			list = list->next;
		
		list->next = new_item;
	}		
}

/*---------------------------------------------------------------------------*/
// Spliting a string into multiple strings
// Split char is '/'
// e.g. in:  "I2C/SEND/12/0"
//      out: "I2C" -> "SEND" -> "12" -> "0" 
linked_list_t * process_request(char* request) {
	
	int i;
	int start = 0;
	int stop = 0;
	
	linked_list_t * list = NULL;
		
	while (stop < strlen(request)) {
				
		if (request[start] == '/') {
			start++;
			stop++;
		} else if (request[stop] == '/' || stop >= (strlen(request)-1)) {
			
			// Create the new node
			linked_list_t * new_node = malloc(sizeof(linked_list_t));
			new_node->next = NULL;
			
			if (request[stop] == '/') {
				new_node->data = malloc(sizeof(char) * (stop-start+1));
				// Fill in data
				for (i = 0; i < (stop-start); i++)
					(new_node->data)[i] = request[start+i];
			} else {
				new_node->data = malloc(sizeof(char) * (stop-start+2));
				// Fill in data
				for (i = 0; i < (stop-start+1); i++)
					(new_node->data)[i] = request[start+i];
			}
			(new_node->data)[i] = '\0';

			// Add it to the list
			if (list)
			  add_at_the_end(list, new_node);
			else
			  list = new_node;
			
			// Move start pointer after the copied string
			stop++;
			start = stop;
		} else 
			stop++;
	} 

	return list;
}

/*---------------------------------------------------------------------------*/
void free_linked_list(linked_list_t * list) {
   linked_list_t *temp;

   while (list) {
       free(list->data);
       temp = list;
       list = list->next;
       free(temp);
    }		
}

        
/*---------------------------------------------------------------------------*/
static int callback_tcp_input(struct tcp_socket *s, void *ptr,
      const uint8_t *inputptr, int inputdatalen) {
    
    printf("Received %d bytes : %s\n", inputdatalen, inputptr);
	
	char * buff = malloc(inputdatalen+1);
	strcpy(buff, inputptr);
	buff[inputdatalen] = '\0';
	
    process_post(&main_process, tcp_data_received, buff);

    /* Discard everything */
    return 0; /* all data consumed */
}

/*---------------------------------------------------------------------------*/
static void callback_tcp_event(struct tcp_socket *s, void *ptr, tcp_socket_event_t ev) {
	last_tcp_event = ev;
	process_post(&main_process, tcp_event, NULL);
}

/*---------------------------------------------------------------------------*/
struct tcp_socket * new_tcp_connection(const char * address, uint16_t port){
    static struct tcp_socket conn;

    if(address){
        uip_ip6addr_t addr;
        if(!uiplib_ipaddrconv(address, &addr))
        {
            printf("TCP: Failed to convert IP: %s\n", address);
            return NULL;
        }
        
 		ipv6_add_default_route(address, 0);

		tcp_socket_register(&conn, NULL,
               inputbuf, sizeof(inputbuf),
               outputbuf, sizeof(outputbuf),
               callback_tcp_input,
               callback_tcp_event);
               		    
        if (tcp_socket_connect(&conn, &addr, port) == 1)
        	return &conn;
        else
        	return NULL;
        	
    } else {
        printf("TCP: No IP provided\n");
        return NULL;
    }
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(main_process, ev, data)
{
    PROCESS_BEGIN();
	
	static struct tcp_socket * socket; 
     
    static linked_list_t *request;
    static linked_list_t *request_p;
            
    static bus_type bus;
    static cmd_type cmd;

    static char flash_buff[256];
    static uint8_t device_name_length;
    static char* device_name = NULL;
    
    static char buff[OUTPUTBUFSIZE];

    tcp_event = process_alloc_event();
    tcp_data_received = process_alloc_event();

    
    leds_on(LED1);
    leds_on(LED2);
    
    // Init serial comm for debug
    pic32_uart3_init(9600, 0);
                    
    INIT_NETWORK_DEBUG();
    {
        PRINTF("===START===\n");
        leds_off(LED2);

	    // --- Read device name from the flash --- //
	    
	    // Flash Bank
	    //  0 : Length of device name
	    //  1 -> x : Device name
	    
	    // Read from address 0 
	    COFFEE_READ(flash_buff, sizeof(flash_buff), 0);
	    device_name_length = flash_buff[0];
	    
	    if (device_name_length == 0 || device_name_length == 255) {
	      PRINTF("No device name set, setting device name to \"Clicker\"\n");
	      
	      device_name_length = strlen("Clicker");
	      device_name = malloc(device_name_length);
	      sprintf(device_name, "Clicker");
	      
	      flash_buff[0] = device_name_length;
	      memcpy(flash_buff+1, device_name, device_name_length);
	      
	      // Write device_name_length and device_name from offset 0
	      COFFEE_WRITE(flash_buff, device_name_length + 1, 0);
	      
	    } else {
	    	device_name = malloc(device_name_length);
	    	memcpy(device_name, flash_buff+1, device_name_length);
	        PRINTF("Device name: %s\n", device_name);
	    }
	    
	    // --- End read device name from the flash --- //
              

        // --- TCP connection --- //

		socket = new_tcp_connection(SERVER_IP_ADDR, SERVER_PORT);		
		if (socket)
			PRINTF("Connecting...\n\r");
		else
			PRINTF("Connection error\n\r");
			
		PROCESS_WAIT_EVENT_UNTIL(ev == tcp_event);
		
        if (last_tcp_event == TCP_SOCKET_ABORTED)
            PRINTF("Aborted\n");
        if (last_tcp_event == TCP_SOCKET_TIMEDOUT)
            PRINTF("Timeout\n");
        if (last_tcp_event == TCP_SOCKET_CLOSED)
            PRINTF("Closed\n");
               
        if(last_tcp_event == TCP_SOCKET_CONNECTED) {
        	
        	// --- TCP hand-check --- //
        			
			PRINTF("Connected\n\r");
			
			// Send Hello
			sprintf(buff, "HELLO/%s", device_name);
			PRINTF("Sending: '%s'\n\r", buff);
			tcp_socket_send_str(socket, buff);
			
			// Wait for Hello to be sent back
			PROCESS_WAIT_EVENT_UNTIL(ev == tcp_data_received);

			static bool running;
	
			if (strcmp(data, "HELLO") == 0) {
				running = true;
				leds_off(LED1);
			} else {
				running = false;
				PRINTF("Error, expected to receive 'HELLO' but got '%s'\n", data);	
			}

        	// --- End TCP hand-check --- //

	        while(running) {
	        	
				PROCESS_YIELD();
				
				// --- TCP data received --- //
				if(ev == tcp_data_received) {

					static char * buff;
					static int len;
					len = strlen(data);
					buff = malloc(len + 1);
					memcpy(buff, data, len);
					buff[len] = '\0';

	            	free(data);

					PRINTF("Received: '%s'\n\r", buff);
					
	            	request = process_request(buff);
	            	request_p = request;
	            	
	            	free(buff);
	            	

		            // --- Spec for 6lowpan messages --- //
		            //
		            //  The messages sent over 6lowpan are strings
		            //  separated by '/'.
		            //  This is passed to process_request() in 
		            //  order to split them into multiple strings.
		            //  We now have 'p' pointing on the first of
		            //  these strings.
		            //
		            // --- Spec for Server->Client messages --- //
					//
		            //  Standard formating :
		            //    "bus/command/address/data/data"
		            //
		            //  bus : Can be I2C, SPI, GPIO, UART or CORE
		            //  command : Can be INIT, RELEASE, WRITE or READ
		            //  address : Only for READ and WRITE of
		            //            I2C, SPI and GPIO, base 10
		            //            For CORE, can only be DEVICE_NAME
		            //  data : 
		            //    For I2C and SPI : 
		            //      - Base 10
		            //      - Only required for READ and WRITE
		            //      - For WRITE : Can be one or more occurence (burst)
		            //      - For READ : One occurence, number of byte to be read
		            //    For GPIO : 
		            //      - Base 2
		            //      - One occurence
		            //      - Only required for WRITE
		            //    For UART : 
		            //      - String
		            //      - One occurence
		            //      - Only required for WRITE
		            //
		            //  Comments :
		            //    - For CORE, command can only be WRITE, address can
		            //        only be DEVICE_NAME
		            //
		            //  To be clarified :
		            //    - UART READ : How is this suposed to work ?
		            //    - GPIO address : Format ?
		            //
		            //  Limitations : The max length of the TCP request is
		            //    defined by INPUTBUFSIZE
		            //
		            //  Exceptions :
		            //    - At boot-up of a 6lowpan client, there is a
		            //        hand-check process to make sure the connection 
		            //        is up. The client send "HELLO/device_name"
		            //        (with device_name being the device name). And
		            //        then the server reply "HELLO" 
		            //        which would be an invalid request if this wasn't
		            //        at boot-up.
		            //
		            // --- Spec for Client->Server messages --- //
					//
		            //  Standard formating :
		            //    "command/data"
		            //
		            //  command : Can be HELLO or REPLY
		            //  data : 
		            //    For HELLO : Device name of the sender
		            //    For RELPY : tbd
					//
		            // --- End spec for Client->Server messages --- //
		            
		                	
		            if (strcmp(request->data, "I2C") == 0)
		            	bus = I2C;
		            else if (strcmp(request->data, "SPI") == 0)
		            	bus = SPI;
		            else if (strcmp(request->data, "GPIO") == 0)
		            	bus = GPIO;
		            else if (strcmp(request->data, "UART") == 0)
		            	bus = UART;
		            else if (strcmp(request->data, "CORE") == 0)
		            	bus = CORE;
		            else
		            	PRINTF("Error, %s unknown\n", request->data);
		            
		            request = request->next;
		            
		            if (strcmp(request->data, "INIT") == 0)
		            	cmd = INIT;
		            else if (strcmp(request->data, "RELEASE") == 0)
		            	cmd = RELEASE;
		            else if (strcmp(request->data, "WRITE") == 0)
		            	cmd = WRITE;
		            else if (strcmp(request->data, "READ") == 0)
		            	cmd = READ;
		            else
		            	PRINTF("Error, %s unknown\n", request->data);
		
		            request = request->next;
		            
		            if (bus == I2C && cmd == INIT)
		            	i2c_init();
		
		            if (bus == I2C && cmd == RELEASE)
		            	i2c_release();
					
					// TODO: Extends to multi bytes
		            if (bus == I2C && cmd == WRITE)
		            	i2c_write_byte(atol(request->data), atoi(request->next->data));
		
					// TODO: Extends to multi bytes
		            if (bus == I2C && cmd == READ) {
		            	static char data_read = 0;
		            	i2c_read_byte(atol(request->data), &data_read);
				
				        sprintf(buff, "RELPY/%d", data_read);
		        		PRINTF("Sending: '%s'\n", buff);
						tcp_socket_send_str(socket, buff);
		            }
		            
		            if (bus == GPIO && cmd == WRITE) {
		            	static char* port;
		            	static int value;
		            	
		            	port = request->data;
		            	request = request->next;
		            	value = atoi(request->data);            		
		            	
		            	if (strcmp(port, "LED1") == 0) {
		            		if (value)
		            			leds_on(LED1);
							else
								leds_off(LED1);
		            	}
		            	if (strcmp(port, "LED2") == 0) {
		            		if (value)
		            			leds_on(LED2);
							else
								leds_off(LED2);
		            	}
		            }
		            
		            // Change the device name
		            if (bus == CORE && cmd == WRITE) {
		            	if (strcmp(request->data, "DEVICE_NAME") == 0) {
	   			            request = request->next;
							
							// Free the old name
							free(device_name);
							
							// Set the new device name
							device_name_length = strlen(request->data);
							device_name = malloc(device_name_length);
							memcpy(device_name, request->data, device_name_length);
							
							// Prepare the values to be writen in the flash
							flash_buff[0] = device_name_length;
							memcpy(flash_buff+1, device_name, device_name_length);
							
							// Write device_name_length and device_name from offset 0
							COFFEE_WRITE(flash_buff, device_name_length + 1, 0);
		            	}
		            }
		            
		            free_linked_list(request_p);            	
	 			
	 			// --- TCP event --- //
	 			} else if(ev == tcp_event) {

			        if (last_tcp_event == TCP_SOCKET_ABORTED)
			            PRINTF("Aborted\n");
			        if (last_tcp_event == TCP_SOCKET_TIMEDOUT)
			            PRINTF("Timeout\n");
			        if (last_tcp_event == TCP_SOCKET_CLOSED)
			            PRINTF("Closed\n");
			        
			        if (last_tcp_event == TCP_SOCKET_ABORTED 
			            || last_tcp_event == TCP_SOCKET_TIMEDOUT
			            || last_tcp_event == TCP_SOCKET_CLOSED) {
					        PRINTF("Stopping\n");
					        running = false;
			        }
	 			}
	        }
        }
    }

    PROCESS_END();
}

/*---------------------------------------------------------------------------*/

