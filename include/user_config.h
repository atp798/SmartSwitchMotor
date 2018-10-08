#ifndef __USER_CONFIG_H__
#define __USER_CONFIG_H__

#define DEBUG_PRINT  //for TRACE

#define USE_OPTIMIZE_PRINTF

//#define FOTA

#define CONF_HOLDER			0x2A17

#define INIT_AP_PASSWD		"esp8266ex"

#define WEB_SERV_PORT		80
#define TCP_SERV_PORT		5050

#define STA_TIMER_INTERVAL	5000
#define SNTP_QUERY_INTVL	100

#define COMM_PREFIX			0x417A	// Az
#define COMM_HEADLEN		4
#define REPORT_PREFIX		0x5A39	// Z9
#define REPORT_HEADLEN		4

#define MQTT_DEFAULT_PORT		60
#define MQTT_ENABLE_SSL			0
#define MQTT_KEEPALIVE			120

#define MAX_DNS_RETRY		10

#define WEBSERV_TASK_PRIO	USER_TASK_PRIO_0  	//0 is the lowest priority
#define TCPCOMM_TASK_PRIO	USER_TASK_PRIO_1	//priority 2 is reserved for mqtt client

#define TOUCH_ENABLE

//#define DEBUG_PRINT
//#define GLOBAL_DEBUG_ON
#endif

