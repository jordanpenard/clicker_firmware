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


#define SERVER_IP_ADDR "2001:1418:100::1"

#define SERVER_PORT 3000
#define CLIENT_PORT 3001

#define UDP_BUFFER_SIZE 1024


typedef enum {I2C, SPI, GPIO, UART, CORE} bus_type;
typedef enum {INIT, RELEASE, WRITE, READ} cmd_type;


typedef struct linked_list { 
	char* data;
	struct linked_list * next; 
} linked_list_t;


// e.g. in:  (a -> b -> 0) (c -> 0)
//      out: a -> b -> c -> 0
void add_at_the_end(linked_list_t *list, linked_list_t *new_item) {
	
	if (list) {
		while (list->next)
			list = list->next;
		
		list->next = new_item;
	}		
}

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

void free_linked_list(linked_list_t * list) {
   linked_list_t *temp;

   while (list) {
       free(list->data);
       temp = list;
       list = list->next;
       free(temp);
    }		
}

        
PROCESS(main_process, "Main process");
AUTOSTART_PROCESSES(&main_process);
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(main_process, ev, data)
{
    PROCESS_BEGIN();

    static struct uip_udp_conn * socket;
    static char udp_buffer[UDP_BUFFER_SIZE];
 
    static linked_list_t *request;
    static linked_list_t *request_p;
            
    static bus_type bus;
    static cmd_type cmd;

    static char flash_buff[256];
    static uint8_t device_name_length;
    static char* device_name = NULL;

	// Clean the udp_buffer	 
    memset(udp_buffer, '\0', UDP_BUFFER_SIZE); 
          
    leds_on(LED1);

    // Init serial comm for debug
    pic32_uart3_init(9600, 0);
                    
    INIT_NETWORK_DEBUG();
    {
        PRINTF("===START===\n");
        leds_on(LED2);

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
              
        ipv6_add_default_route(SERVER_IP_ADDR, 0);

        PRINTF("Creating connection...\n");
        socket = udp_new_connection(CLIENT_PORT, SERVER_PORT, SERVER_IP_ADDR);
        PROCESS_WAIT_UDP_CONNECTED();

        // --- UDP hand-check --- //

        sprintf(udp_buffer, "Server/HELLO/%s", device_name);

        PRINTF("Sending \"%s\"\n", udp_buffer);
        udp_packet_send(socket, udp_buffer, strlen(udp_buffer));
        PROCESS_WAIT_UDP_SENT();

        static char* expected_reply;
        expected_reply = malloc(strlen(device_name) + strlen("/HELLO"));
        sprintf(expected_reply, "%s/HELLO", device_name);
        
        PRINTF("Wait for the server to reply \"%s\"\n", expected_reply);
	    do {
	      // Clean the udp_buffer	 
          memset(udp_buffer, '\0', strlen(udp_buffer));   
        	
          PROCESS_WAIT_UDP_RECEIVED();
	      udp_packet_receive(udp_buffer, sizeof(udp_buffer), NULL);
	    } while(strcmp(udp_buffer, expected_reply) != 0);
	    
	    // --- End UDP hand-check --- //
        
        // Switch off the leds, ready for normal operations
        leds_off(LED1);
        leds_off(LED2);

        while(1) {
        	
        	// Clean the udp_buffer	 
        	memset(udp_buffer, '\0', strlen(udp_buffer));       
	        
	        PRINTF("WAIT_UDP_RECEIVED\n");
	        PROCESS_WAIT_UDP_RECEIVED();
	        udp_packet_receive(udp_buffer, sizeof(udp_buffer), NULL);
	          	        	        
	        PRINTF("Received: %s\n", udp_buffer);
            
            request = process_request(udp_buffer);
            request_p = request;
            
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
            //    "device_name/bus/command/address/data/data"
            //
            //  device_name : This is suposed to be unique
            //                "Server" and "All" are reserved
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
            //  Limitations : The max length of the UDP request is
            //    defined by UDP_BUFFER_SIZE
            //
            //  Exceptions :
            //    - At boot-up of a 6lowpan client, there is a
            //        hand-check process to make sure the connection 
            //        is up. The client send "Server/HELLO/device_name"
            //        (with device_name being the device name). And
            //        then the server reply "device_name/HELLO" 
            //        which would be an invalid request if this wasn't
            //        at boot-up.
            //
            // --- Spec for Client->Server messages --- //
			//
            //  Standard formating :
            //    "device_name/command/data"
            //
            //  device_name : Should only be "Server" 
            //  command : Can be HELLO or REPLY
            //  data : 
            //    For HELLO : Device name of the sender
            //    For RELPY : tbd
			//
            // --- End spec for Client->Server messages --- //
            
            // If the message is for this device or for all devices
            if (strcmp(request->data, device_name) == 0 
                || strcmp(request->data, "All") == 0) {
                	
	            request = request->next;

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
			
			        sprintf(udp_buffer, "%d", data_read);
	        		PRINTF("Sending data: %s\n", udp_buffer);
	        		udp_packet_send(socket, udp_buffer, strlen(udp_buffer));
			        PROCESS_WAIT_UDP_SENT();
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
            }
        }
    }

    PROCESS_END();
}

/*---------------------------------------------------------------------------*/

