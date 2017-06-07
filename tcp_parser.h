#ifndef TCP_PARSER_H
#define TCP_PARSER_H

#include "datatypes.h"
#include "routing.h"

typedef struct
{
	// Where to write when receiving. This field is ignored for tx. 
	// Remember to use packed structs
	void* p_data; 
	// Message ID
	uint8_t mid;
	// Number of bytes of data expected / sent
	int size;
	// Zero-terminated string: Interpretation of the bytes:
	char types[32]; 
	/*
		'b' int8_t
		'B' uint8_t
		's' int16_t
		'S' uint16_t
		'i' int32_t
		'I' uint32_t
		'l' int64_t
		'L' uint64_t
	*/
	int ret;
} tcp_message_t;


#define TCP_CR_DEST_MID    55
typedef struct __attribute__ ((packed))
{
	int32_t x;
	int32_t y;
	int8_t backmode;
} tcp_cr_dest_t;

extern tcp_cr_dest_t   msg_cr_dest;

#define TCP_CR_ROUTE_MID    56
typedef struct __attribute__ ((packed))
{
	int32_t x;
	int32_t y;
	int8_t dummy;
} tcp_cr_route_t;

extern tcp_cr_route_t   msg_cr_route;


#define TCP_RC_POS_MID    130
typedef struct __attribute__ ((packed))
{
	int16_t ang;
	int32_t x;
	int32_t y;
} tcp_rc_pos_t;

extern tcp_message_t   msgmeta_rc_pos;
extern tcp_rc_pos_t    msg_rc_pos;

#define TCP_RC_LIDAR_MID   131
#define TCP_RC_DBG_MID     132
#define TCP_RC_SONAR_MID   133
#define TCP_RC_BATTERY_MID 134
#define TCP_RC_ROUTEINFO_MID 135

int tcp_parser(int sock);

int tcp_send_msg(tcp_message_t* msg_type, void* msg);

void tcp_send_lidar(lidar_scan_t* p_lid);
void tcp_send_hwdbg(int32_t* dbg);
void tcp_send_sonar(sonar_scan_t* p_son);
void tcp_send_battery();
void tcp_send_route(route_unit_t **route);

#endif
