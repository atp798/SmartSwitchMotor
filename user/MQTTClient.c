#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "user_config.h"
#include "GlobalInfo.h"
#include "FlashParam.h"
#include "os_type.h"
#include "mem.h"
#include "UserIO.h"
#include "mqtt/debug.h"
#include "mqtt/mqtt.h"

static const char topicCtrlSwitch[] = "control/switch";			//for subscribe
static const char topicQuerySwitch[] = "query/switch_status";	//for subscribe
static const char topicReportSwitch[] = "report/switch_status"; //for publish

extern struct FlashProtectParam stFlashProtParam;
extern char RelayStatus;

bool MQTTClientOn = false;
MQTT_Client MQTTClient;

LOCAL os_timer_t tmMQTTreport;

LOCAL void ICACHE_FLASH_ATTR
MQTTPublishStatus(int qos){
    char pubBuf[255];
    os_memset(pubBuf,0,sizeof(pubBuf));
    os_sprintf(pubBuf,"%s%s",stFlashProtParam.MQTTClientID,GetRelayStatus() == RELAY_ON?"on":"off");
    MQTT_Publish(&MQTTClient, topicReportSwitch, pubBuf, os_strlen(pubBuf), qos, 0);
}

void ICACHE_FLASH_ATTR
MQTTReportTimerCB(){
    os_timer_disarm(&tmMQTTreport);

    MQTTPublishStatus(0);

    os_timer_setfn(&tmMQTTreport,(os_timer_func_t *)MQTTReportTimerCB,NULL); //set callback func
    os_timer_arm(&tmMQTTreport,STA_TIMER_INTERVAL,0); //set timer interval,unit:ms
}

void ICACHE_FLASH_ATTR
mqttConnectedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    INFO("MQTT: Connected\r\n");
    MQTT_Subscribe(client, (char*)topicCtrlSwitch, 1);
    MQTT_Subscribe(client, (char*)topicQuerySwitch, 1);

    MQTTPublishStatus(1);
}

void ICACHE_FLASH_ATTR
mqttDisconnectedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    INFO("MQTT: Disconnected\r\n");
}

void ICACHE_FLASH_ATTR
mqttPublishedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    INFO("MQTT: Published\r\n");
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

void ICACHE_FLASH_ATTR
mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
    MQTT_Client* client = (MQTT_Client*)args;
    char *topicBuf = (char*)os_zalloc(topic_len+1),
            *dataBuf = (char*)os_zalloc(data_len+1);

    os_memcpy(topicBuf, topic, topic_len);
    topicBuf[topic_len] = 0;

    os_memcpy(dataBuf, data, data_len);
    dataBuf[data_len] = 0;

    INFO("Receive topic:%d:%s, data:%d:%s \r\n",topic_len, topicBuf, data_len,dataBuf);

    if(os_strstr(dataBuf,stFlashProtParam.MQTTClientID)==dataBuf){
		TRACE("id checked\r\n");
		if(os_strncmp(topicBuf,topicQuerySwitch,sizeof(topicQuerySwitch))==0){
			TRACE("query recv\r\n");
		}else if( os_strncmp(topicBuf,topicCtrlSwitch,sizeof(topicCtrlSwitch))==0){
			TRACE("ctrl recv\r\n");
			if(os_strncmp(dataBuf + os_strlen(stFlashProtParam.MQTTClientID),"on",2)==0){
				RelayOn();
			}else if(os_strncmp(dataBuf + os_strlen(stFlashProtParam.MQTTClientID),"off",3)==0){
				RelayOff();
			}else if(os_strncmp(dataBuf + os_strlen(stFlashProtParam.MQTTClientID),"switch",6)==0){
				RelaySwitch();
			}else if(os_strncmp(dataBuf + os_strlen(stFlashProtParam.MQTTClientID),"TouchDisable",12)==0){
				DisableTouchPin();
			}else if(os_strncmp(dataBuf + os_strlen(stFlashProtParam.MQTTClientID),"TouchEnable",11)==0){
				EnableTouchPin();
			}else if(os_strstr(dataBuf + os_strlen(stFlashProtParam.MQTTClientID),"set-alarm:")!=0){ //esp826601-01set-alarm:2:8:1536662580:1536662583:1::
				char *pStart = dataBuf + os_strlen(stFlashProtParam.MQTTClientID) + 10;
				sint32 val;
				pStart = GetNextNumParam(pStart,&val);
				if(pStart == NULL){
					TRACE("alarm id error");
					goto EXIT_PUBLISH;
				}
				STAlarm stAlm;
				os_memset(&stAlm,0,sizeof(STAlarm));
				stAlm.ID = val;
				pStart = GetNextNumParam(pStart,&val);
				if(pStart == NULL){
					TRACE("alarm repeat mode error");
					goto EXIT_PUBLISH;
				}
				stAlm.RptMode = val;
				pStart = GetNextNumParam(pStart,&val);
				if(pStart == NULL){
					TRACE("alarm start time error");
					goto EXIT_PUBLISH;
				}
				stAlm.Start = val;
				pStart = GetNextNumParam(pStart,&val);
				if(pStart == NULL){
					TRACE("alarm stop time error");
					goto EXIT_PUBLISH;
				}
				stAlm.Stop = val;
				val = -1;
				pStart = GetNextNumParam(pStart,&val);
				if(pStart == NULL && val<0){
					TRACE("alarm repeat interval error");
					goto EXIT_PUBLISH;
				}
				stAlm.RptIntvl = val;
				if(SetAlarm(&stAlm)){
					TRACE("client set alarm success id:%d:%d:%d:%d:%d\r\n",stAlm.ID,stAlm.RptMode,stAlm.Start,stAlm.Stop,stAlm.RptIntvl);
				}else{
					TRACE("client set alarm failed id:%d:%d:%d:%d:%d\r\n",stAlm.ID,stAlm.RptMode,stAlm.Start,stAlm.Stop,stAlm.RptIntvl);
				}
			}else if(os_strstr(dataBuf + os_strlen(stFlashProtParam.MQTTClientID),"disable-alarm:")!=0){
				char *pStart = dataBuf + os_strlen(stFlashProtParam.MQTTClientID) + 14;
				sint32 val = -1;
				pStart = GetNextNumParam(pStart,&val);
				if(pStart == NULL && val<0){
					TRACE("alarm parameter error");
					goto EXIT_PUBLISH;
				}
				DisableAlarm(val);
				TRACE("disable alarm:%d\r\n",val);
			}else if(os_strstr(dataBuf + os_strlen(stFlashProtParam.MQTTClientID),"enable-alarm:")!=0){
				char *pStart = dataBuf + os_strlen(stFlashProtParam.MQTTClientID) + 13;
				sint32 val = -1;
				pStart = GetNextNumParam(pStart,&val);
				if(pStart == NULL && val<0){
					TRACE("alarm parameter error");
					goto EXIT_PUBLISH;
				}
				EnableAlarm(val);
				TRACE("disable alarm:%d\r\n",val);
			}else if(os_strstr(dataBuf + os_strlen(stFlashProtParam.MQTTClientID),"set-id:")!=0){
				os_strcpy(stFlashProtParam.MQTTClientID, dataBuf + os_strlen(stFlashProtParam.MQTTClientID) + 7);
				SaveFlashProtParam();
				TRACE("client id set to:%s\r\n",stFlashProtParam.MQTTClientID);
			}else if(os_strncmp(dataBuf + os_strlen(stFlashProtParam.MQTTClientID),"webserv-on",10)==0){
				stFlashProtParam.WorkStatus |= WEB_SERV_BIT;
				SaveFlashProtParam();
				system_os_post(WEBSERV_TASK_PRIO,WSIG_START_SERV,WEB_SERV_PORT);
			}else if(os_strncmp(dataBuf + os_strlen(stFlashProtParam.MQTTClientID),"webserv-off",11)==0){
				stFlashProtParam.WorkStatus &= ~WEB_SERV_BIT;
				SaveFlashProtParam();
				uint8 WifiMode = STATION_MODE;
				wifi_set_opmode(WifiMode);
			}
		}
	}
EXIT_PUBLISH:
    MQTTPublishStatus(1);

    os_free(topicBuf);
    os_free(dataBuf);
}

void ICACHE_FLASH_ATTR
MQTTClientStop(){
	MQTT_Disconnect(&MQTTClient);
	os_timer_disarm(&tmMQTTreport);
	MQTTClientOn = false;
	TRACE("MQTT client stoped...\r\n");
}

void ICACHE_FLASH_ATTR
MQTTClientConnect(){
    MQTT_Connect(&MQTTClient);
    MQTTClientOn = true;
}

void ICACHE_FLASH_ATTR
MQTTClientInit() {
	MQTT_InitConnection(&MQTTClient, stFlashProtParam.MQTTServAddr, stFlashProtParam.MQTTServPort, 0);

	MQTT_InitClient(&MQTTClient, stFlashProtParam.MQTTClientID, stFlashProtParam.MQTTUserName,stFlashProtParam.MQTTPassword, MQTT_KEEPALIVE, 1);

	//last wish, it's unsupportted on some platform
	MQTT_InitLWT(&MQTTClient, "/lwt", "offline", 0, 0);

	MQTT_OnConnected(&MQTTClient, mqttConnectedCb);
	MQTT_OnDisconnected(&MQTTClient, mqttDisconnectedCb);
	MQTT_OnPublished(&MQTTClient, mqttPublishedCb);
	MQTT_OnData(&MQTTClient, mqttDataCb);

    os_timer_disarm(&tmMQTTreport);
    os_timer_setfn(&tmMQTTreport,(os_timer_func_t *)MQTTReportTimerCB,NULL); //set callback func
    os_timer_arm(&tmMQTTreport,STA_TIMER_INTERVAL,0); //set timer interval,unit:ms

	TRACE("MQTT client started...\r\n");
}
