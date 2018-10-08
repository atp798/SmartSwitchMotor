#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "mem.h"
#include "user_interface.h"
#include "espconn.h"
#include "upgrade.h"
#include "TCPComm.h"
#include "FlashParam.h"
#include "user_config.h"
#include "GlobalInfo.h"
#include "UserIO.h"
#include "MQTTClient.h"
#include "AlarmClock.h"

#ifdef SERVER_SSL_ENABLE
#include "ssl/cert.h"
#include "ssl/private_key.h"
#endif

extern struct FlashProtectParam stFlashProtParam;

os_event_t *TCPTaskQueue;
LOCAL struct espconn connServer;
LOCAL struct espconn connClient;
LOCAL esp_tcp espTcp;
bool TCPServOn = false;
bool TCPClientOn = false;
extern bool MQTTClientOn;

LOCAL os_timer_t tmStation;

LOCAL struct espconn connDNSTmp;
LOCAL esp_tcp tcpDNSTmp;
LOCAL ip_addr_t ipDNS;
LOCAL os_timer_t tmDNS;
LOCAL uint8 DNSRetryCtn = 0;
LOCAL bool DNSFound = false;

extern WifiStaGotIPCallBack staGotIPCB;

LOCAL void TCPResponse(void *arg, const char *pData, uint16 length);

void ICACHE_FLASH_ATTR
StationTimerCB(){
	system_os_post(TCPCOMM_TASK_PRIO, TSIG_REPORT,0x00);
}

void ICACHE_FLASH_ATTR
StationTimerInit(){
    os_timer_disarm(&tmStation);
    os_timer_setfn(&tmStation,(os_timer_func_t *)StationTimerCB,NULL); //set callback func
    os_timer_arm(&tmStation,STA_TIMER_INTERVAL,1); //set timer interval,unit:ms
}

LOCAL void ICACHE_FLASH_ATTR
DNSFoundCB(const char *name, ip_addr_t *ipAddr, void *arg)
{
    struct espconn *pEspConn = (struct espconn *)arg;
    if (ipAddr == NULL) {
    	DNSRetryCtn++;
        TRACE("domain dns not found\r\n");
        return;
    }

    DNSFound = true;
    TRACE("domain dns found "IPSTR"\n",IP2STR(ipAddr));
	TCPClientInit(ipAddr->addr, stFlashProtParam.RemotePort);
	StationTimerInit();
}

LOCAL void ICACHE_FLASH_ATTR
DNSRetryTimerCB(void *arg)
{
    struct espconn *pEspConn = arg;
    TRACE("DNS retry\n");
    if(DNSFound){
	    os_timer_disarm(&tmDNS);
    	return;
    }
    espconn_gethostbyname(pEspConn, (char *)stFlashProtParam.RemoteAddr.Domain, &ipDNS, DNSFoundCB);
	if(DNSRetryCtn >= MAX_DNS_RETRY ){
		system_os_post(TCPCOMM_TASK_PRIO, TSIG_DNS_FAILED, 0x00);
		DNSRetryCtn = 0;
	    os_timer_disarm(&tmDNS);
	}else{
		os_timer_arm(&tmDNS, 5000, 0);
	}
}

void ICACHE_FLASH_ATTR
WifiStaConnCB(){
	if(!TCPServOn){
		TCPServInit(TCP_SERV_PORT);
	}
	if(!TCPClientOn){
		if(stFlashProtParam.Domain){
			struct ip_info stationIP;
			wifi_get_ip_info(STATION_IF,&stationIP);
			memcpy(tcpDNSTmp.local_ip,&stationIP.ip.addr,4);

			connDNSTmp.type = ESPCONN_TCP;
			connDNSTmp.state = ESPCONN_NONE;
			connDNSTmp.proto.tcp = &tcpDNSTmp;

	        DNSFound = false;
		    espconn_gethostbyname(&connDNSTmp,stFlashProtParam.RemoteAddr.Domain , &ipDNS, DNSFoundCB);

		    os_timer_disarm(&tmDNS);
		    os_timer_setfn(&tmDNS, (os_timer_func_t *)DNSRetryTimerCB, &connDNSTmp);
		    os_timer_arm(&tmDNS, 5000, 0);
		}else{
			TCPClientInit(stFlashProtParam.RemoteAddr.IP.addr, stFlashProtParam.RemotePort);
			StationTimerInit();
		}
	}
	if(!MQTTClientOn){
		MQTTClientConnect();
	}
}

void TCPCommTask(os_event_t *e){
	struct espconn *pEspConn;
	char *pData;
	switch(e->sig){
		case TSIG_START_SERV:
			if(!TCPServOn){
				TCPServInit(e->par);
			}
			break;
		case TSIG_REPORT:
			if(!TCPClientOn) return;
			LOCAL char szSendBuf[64];
			os_memset(szSendBuf,0,sizeof(szSendBuf));
			os_sprintf(szSendBuf,"{\"relay\":\"%s\"}",GetRelayStatus==RELAY_ON?"on":"off");
			TCPResponse(&connClient,szSendBuf,(uint16)os_strlen(szSendBuf));
			break;
		case TSIG_WIFI_CHANGE:
			break;
		case TSIG_DISCONN:
			pEspConn = (struct espconn*) (e->par);
			if(espconn_disconnect(pEspConn)!=0){  //error code is ESPCONN_ARG
				TRACE("client disconnect failed, argument illegal\r\n");
			}
			break;
		case TSIG_MQTT_CONF:

			break;
		case TSIG_REMOTE_CONF:
			staGotIPCB = WifiStaConnCB;
			break;
		case TSIG_DNS_FAILED:
			stFlashProtParam.WorkStatus |= WEB_SERV_BIT;
			stFlashProtParam.WorkStatus &= ~REMOTE_SERV_CONF_BIT;
			stFlashProtParam.WorkStatus &= ~TCP_SERV_BIT;
			wifi_set_opmode(STATIONAP_MODE);
			system_os_post(WEBSERV_TASK_PRIO, WSIG_START_SERV, WEB_SERV_PORT);
			break;
		default:
			break;
	}
}

LOCAL void ICACHE_FLASH_ATTR
TCPResponse(void *arg, const char *pData, uint16 length){
    struct espconn * pEspConn = arg;
	char respBuf[64];
	os_memset(respBuf,0,sizeof(respBuf));
	os_printf("response:%s\r\n",pData);

	respBuf[0] = REPORT_PREFIX >> 8;
	respBuf[1] = REPORT_PREFIX & 0xff;
	respBuf[2] = length>>8;
	respBuf[3] = length & 0xff;
	os_memcpy(respBuf+4,pData,length);

#ifdef TCP_SERV_SSL_ENABLE
        espconn_secure_sent(pEspConn, respBuf, length+4);
#else
        espconn_sent(pEspConn,respBuf, length+4);
#endif
}

LOCAL char * ICACHE_FLASH_ATTR
GetNextNumParam(char *pStart,sint32* val){
	if(pStart == NULL){
		return NULL;
	}
	char *pEnd = os_strstr(pStart,":");
	char numBuf[12];
	os_memset(numBuf,0,sizeof(numBuf));
	if(pEnd == NULL){
		pEnd = pStart + os_strlen(pStart);
		os_memcpy(numBuf,pStart,pEnd-pStart);
		*val = atoi(numBuf);
		pStart = NULL;
	}else{
		os_memcpy(numBuf,pStart,pEnd-pStart);
		*val = atoi(numBuf);
		if(pEnd + 1 >= pStart + os_strlen(pStart)){
			pStart =NULL;
		}else{
			pStart = pEnd + 1;
		}
	}
	return pStart;
}

LOCAL void ICACHE_FLASH_ATTR
TCPServRecvCB(void *arg, char *pDataRecv, unsigned short length)
{
	struct espconn *pEspConn = arg;
	char respBuf[64];
	os_memset(respBuf,0,sizeof(respBuf));

	TRACE("recved:%02x%02x%02x%02x%s,len:%d\r\n",pDataRecv[0],pDataRecv[1],pDataRecv[2],pDataRecv[3],pDataRecv+4,length);
	if(length>=COMM_HEADLEN && (pDataRecv[0]==(COMM_PREFIX >> 8)) && (pDataRecv[1] == (COMM_PREFIX & 0xff )) ){
		uint16 len;
		len = (pDataRecv[2]<<8) + pDataRecv[3];
		if(length-COMM_HEADLEN == len){
			if(os_strncmp(pDataRecv+COMM_HEADLEN,"touch-enable",12)==0){
				EnableTouchPin();
			}else if(os_strncmp(pDataRecv+COMM_HEADLEN,"touch-disable",13)==0){
				DisableTouchPin();
			}else if(os_strncmp(pDataRecv+COMM_HEADLEN,"relay-on",8)==0){
		        RelayOn();
			}else if(os_strncmp(pDataRecv+COMM_HEADLEN,"relay-off",9)==0){
		        RelayOff();
			}else if(os_strncmp(pDataRecv+COMM_HEADLEN,"relay-switch",12)==0){
		        RelaySwitch();
			}else if(os_strncmp(pDataRecv+COMM_HEADLEN,"query-status",12)==0){
				os_sprintf(respBuf, GetRelayStatus()==RELAY_ON?"on":"off");
				goto EXIT_RESPONSE;
			}else if(os_strstr(pDataRecv+COMM_HEADLEN,"set-alarm:")!=0){
				//Az**set-alarm:0:1:1536625672:1536625672:100
				//Azset-alarm:0:8:1536654312:1536654342:1::
				char *pStart = pDataRecv+COMM_HEADLEN + 10;
				sint32 val;
				pStart = GetNextNumParam(pStart,&val);
				if(pStart == NULL){
					os_sprintf(respBuf,"alarm id error");
					goto EXIT_RESPONSE;
				}
				STAlarm stAlm;
				os_memset(&stAlm,0,sizeof(STAlarm));
				stAlm.ID = val;
				pStart = GetNextNumParam(pStart,&val);
				if(pStart == NULL){
					os_sprintf(respBuf,"alarm repeat mode error");
					goto EXIT_RESPONSE;
				}
				stAlm.RptMode = val;
				pStart = GetNextNumParam(pStart,&val);
				if(pStart == NULL){
					os_sprintf(respBuf,"alarm start time error");
					goto EXIT_RESPONSE;
				}
				stAlm.Start = val;
				pStart = GetNextNumParam(pStart,&val);
				if(pStart == NULL){
					os_sprintf(respBuf,"alarm stop time error");
					goto EXIT_RESPONSE;
				}
				stAlm.Stop = val;
				val = -1;
				pStart = GetNextNumParam(pStart,&val);
				if(pStart == NULL && val<0){
					os_sprintf(respBuf,"alarm repeat interval error");
					goto EXIT_RESPONSE;
				}
				stAlm.RptIntvl = val;
				if(SetAlarm(&stAlm)){
					TRACE("client set alarm success id:%d:%d:%d:%d:%d\r\n",stAlm.ID,stAlm.RptMode,stAlm.Start,stAlm.Stop,stAlm.RptIntvl);
				}else{
					TRACE("client set alarm failed id:%d:%d:%d:%d:%d\r\n",stAlm.ID,stAlm.RptMode,stAlm.Start,stAlm.Stop,stAlm.RptIntvl);
				}
			}else if(os_strstr(pDataRecv+COMM_HEADLEN,"disable-alarm:")!=0){
				char *pStart = pDataRecv+COMM_HEADLEN + 14;
				sint32 val = -1;
				pStart = GetNextNumParam(pStart,&val);
				if(pStart == NULL && val<0){
					os_sprintf(respBuf,"alarm parameter error");
					goto EXIT_RESPONSE;
				}
				DisableAlarm(val);
				TRACE("disable alarm:%d\r\n",val);
			}else if(os_strstr(pDataRecv+COMM_HEADLEN,"enable-alarm:")!=0){
				char *pStart = pDataRecv+COMM_HEADLEN + 13;
				sint32 val = -1;
				pStart = GetNextNumParam(pStart,&val);
				if(pStart == NULL && val<0){
					os_sprintf(respBuf,"alarm parameter error");
					goto EXIT_RESPONSE;
				}
				EnableAlarm(val);
				TRACE("disable alarm:%d\r\n",val);
			}else if(os_strstr(pDataRecv+COMM_HEADLEN,"set-id:")!=0){
				os_strcpy(stFlashProtParam.MQTTClientID, pDataRecv + COMM_HEADLEN + 7);
				SaveFlashProtParam();
				TRACE("client id set to:%s\r\n",stFlashProtParam.MQTTClientID);
			}else if(os_strncmp(pDataRecv+COMM_HEADLEN,"webserv-on",10)==0){
				stFlashProtParam.WorkStatus |= WEB_SERV_BIT;
				SaveFlashProtParam();
				system_os_post(WEBSERV_TASK_PRIO,WSIG_START_SERV,WEB_SERV_PORT);
			}else if(os_strncmp(pDataRecv+COMM_HEADLEN,"webserv-off",11)==0){
				stFlashProtParam.WorkStatus &= ~WEB_SERV_BIT;
				SaveFlashProtParam();
				uint8 WifiMode = STATION_MODE;
				wifi_set_opmode(WifiMode);
			}else{
				os_sprintf(respBuf,"unrecognized command");
				goto EXIT_RESPONSE;
			}
			os_sprintf(respBuf,"ok");
		}else{
			os_sprintf(respBuf,"length check failed");
		}
	}else{
		os_sprintf(respBuf,"frame head check error");
	}
EXIT_RESPONSE:
	TCPResponse(pEspConn,respBuf,os_strlen(respBuf));
}

LOCAL void ICACHE_FLASH_ATTR
TCPServSentCB(void *arg)
{
}

LOCAL ICACHE_FLASH_ATTR
void TCPServRecon(void *arg, sint8 err){
    struct espconn *pEspConn = arg;
	TCPServOn = false;
	TRACE("client : "IPSTR":%d come up with fault, try to reconnect\r\n", IP2STR(pEspConn->proto.tcp->remote_ip),pEspConn->proto.tcp->remote_port);

	sint8 retCode = 0;
#ifdef TCP_SERV_SSL_ENABLE
    espconn_secure_set_default_certificate(default_certificate, default_certificate_len);
    espconn_secure_set_default_private_key(default_private_key, default_private_key_len);
    retCode = espconn_secure_accept(&connServer);
#else
    retCode = espconn_accept(&connServer);
	//espconn_regist_time(&connServer,600, 1);			// client connectted timeout, unit: second, 0~7200, this api don't support ssl, third param 1 is only apply to this tcp, 0 for all
#endif
	if(retCode == 0){
		TCPServOn = true;
	}
}

LOCAL ICACHE_FLASH_ATTR
void TCPServDiscon(void *arg){
    struct espconn *pEspConn = arg;
    TRACE("client: "IPSTR":%d disconnected\r\n", IP2STR(pEspConn->proto.tcp->remote_ip),pEspConn->proto.tcp->remote_port);
}

LOCAL void ICACHE_FLASH_ATTR
TCPServListen(void *arg){
    struct espconn *pEspConn = arg;
    TRACE("client: "IPSTR":%d connected\r\n", IP2STR(pEspConn->proto.tcp->remote_ip),pEspConn->proto.tcp->remote_port);

    espconn_regist_recvcb(pEspConn, TCPServRecvCB);
    espconn_regist_sentcb(pEspConn, TCPServSentCB);
    espconn_regist_reconcb(pEspConn, TCPServRecon);
    espconn_regist_disconcb(pEspConn, TCPServDiscon);
}

void ICACHE_FLASH_ATTR
TCPServInit(uint32 port){
	struct ip_info stationIP;
	wifi_get_ip_info(STATION_IF,&stationIP);
	memcpy(espTcp.local_ip,&stationIP.ip.addr,4);

	connServer.type = ESPCONN_TCP;
	connServer.state = ESPCONN_NONE;
	connServer.proto.tcp = &espTcp;
	connServer.proto.tcp->local_port = port;
	espconn_regist_connectcb(&connServer, TCPServListen);

	sint8 retCode = 0;
#ifdef TCP_SERV_SSL_ENABLE
    espconn_secure_set_default_certificate(default_certificate, default_certificate_len);
    espconn_secure_set_default_private_key(default_private_key, default_private_key_len);
    retCode = espconn_secure_accept(&connServer);
#else
    retCode = espconn_accept(&connServer);
	espconn_regist_time(&connServer,600, 1);			// client connectted timeout, unit: second, 0~7200, this api don't support ssl, third param 1 is only apply to this tcp, 0 for all
#endif
	if(retCode == 0){
		TCPServOn = true;
		TRACE("tcp server start up, listen on port:%d\r\n", port);
	}else{
		TRACE("tcp server start failed, code:%d\r\n",retCode);
	}
}

/********************* TCP Client **********************/


static void ICACHE_FLASH_ATTR
TCPClientSendCB(void *arg) {
}

static void ICACHE_FLASH_ATTR
TCPClientRecvCB(void *arg, char *pDataRecv, unsigned short len) {
	TRACE("tcp client received:%s\n",pDataRecv);
}

static void ICACHE_FLASH_ATTR
TCPClientReconnCB(void *arg,sint8 err) {
    struct espconn *pEspConn = arg;
    TCPClientOn = false;
    TRACE("fault happened, try to reconnect to server: "IPSTR":%d\n", IP2STR(pEspConn->proto.tcp->remote_ip),pEspConn->proto.tcp->remote_port);

    switch(err){
    case ESPCONN_TIMEOUT:
    	break;
    case ESPCONN_ABRT: //exception
        break;
    case ESPCONN_RST:  //exception when reset
        break;
    case ESPCONN_CLSD: //error when close
        break;
    case ESPCONN_CONN:  //tcp connect failed
        break;
    case ESPCONN_HANDSHAKE:  //ssl handshake failed
        break;
    case ESPCONN_SSL_INVALID_DATA: //ssl data proceed fault
        break;
    default:
        break;
    }

    /*  this code will cause infinite loop ,and it's unnecessary, the timer will do this
	sint8 retCode = 0;
#ifdef TCP_CLIENT_SSL_ENABLE
	//espconn_secure_ca_enable(0x01, SSL_CA_ADDR);
	espconn_secure_cert_req_enable(0x01, SSL_CLIENT_KEY_ADDR);
	retCode = espconn_secure_connect(&TCPClientConnCB);
#else
	retCode = espconn_connect(&connClient);
#endif
	if(retCode == 0){ TCPClientOn = true;}
	*/
}

static void ICACHE_FLASH_ATTR
TCPClientDisconnCB(void *arg) {
    struct espconn *pEspConn = arg;
    TRACE("disconnected from server: "IPSTR":%d\r\n", IP2STR(pEspConn->proto.tcp->remote_ip),pEspConn->proto.tcp->remote_port);
    TCPClientOn = false;
}

static void ICACHE_FLASH_ATTR
TCPClientConnCB(void *arg) {
    struct espconn *pEspConn = arg;
    TRACE("connected to server: "IPSTR":%d\r\n", IP2STR(pEspConn->proto.tcp->remote_ip),pEspConn->proto.tcp->remote_port);

    sint8 retCode = espconn_set_opt(pEspConn,ESPCONN_KEEPALIVE);
    if(retCode == 0){
    	TRACE("set keepalive success\r\n");
    	uint16 optVal;
    	espconn_get_keepalive(pEspConn,ESPCONN_KEEPIDLE,&optVal);
    	TRACE("keep idle:%d\n",optVal);
    	espconn_get_keepalive(pEspConn,ESPCONN_KEEPINTVL,&optVal);
    	TRACE("keep interval:%d\n",optVal);
    	espconn_get_keepalive(pEspConn,ESPCONN_KEEPCNT,&optVal);
    	TRACE("keep keepcount:%d\n",optVal);
    }else{
    	TRACE("set keepalive failed\r\n");
    }

	espconn_regist_recvcb(pEspConn, TCPClientRecvCB);
	espconn_regist_sentcb(pEspConn, TCPClientSendCB);
	espconn_regist_disconcb(pEspConn, TCPClientDisconnCB);
	espconn_regist_reconcb(pEspConn, TCPClientReconnCB); //callback to deal with faults

	u8 *sendBuf = "hi";
	TCPResponse(pEspConn,sendBuf,os_strlen(sendBuf));
}

bool ICACHE_FLASH_ATTR
TCPClientInit(uint32 ipAddr, uint16 port) {
	if(TCPClientOn)return true;
	static esp_tcp espTCP;
	//uint32 u32IP = ipaddr_addr(ipAddr);

	TRACE("Initialize tcp client...\r\n");

	connClient.type = ESPCONN_TCP;
	connClient.state = ESPCONN_NONE;
	connClient.proto.tcp = &espTCP;
	os_memcpy(connClient.proto.tcp->remote_ip, &ipAddr, 4);//set server ip
	connClient.proto.tcp->remote_port = port;			//set server port
	connClient.proto.tcp->local_port = espconn_port();

	espconn_regist_connectcb(&connClient, TCPClientConnCB);

	sint8 retCode = 0;
#ifdef TCP_CLIENT_SSL_ENABLE
	//espconn_secure_ca_enable(0x01, SSL_CA_ADDR);
	espconn_secure_cert_req_enable(0x01, SSL_CLIENT_KEY_ADDR);
	retCode = espconn_secure_connect(&TCPClientConnCB);
#else
	retCode = espconn_connect(&connClient);
#endif

	if(retCode == 0){ TCPClientOn = true;}
	TRACE("connect to "IPSTR":%d return code:%d\r\n", IP2STR(connClient.proto.tcp->remote_ip),connClient.proto.tcp->remote_port,retCode);
    return TCPClientOn;
}


