#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "user_interface.h"
#include "eagle_soc.h"
#include "FlashParam.h"
#include "GlobalInfo.h"
#include "UserIO.h"

extern uint8 LEDMode;
extern struct FlashProtectParam stFlashProtParam;
LOCAL os_timer_t tmWifiStat;
WifiStaGotIPCallBack staGotIPCB = NULL;

void ICACHE_FLASH_ATTR
WifiStatusTimerCB(){
	os_timer_disarm(&tmWifiStat);
	uint8 connStatus = wifi_station_get_connect_status();
	struct ip_info ipConfig;
	wifi_get_ip_info(STATION_IF, &ipConfig);

	TRACE("wifi station connect status:%d\r\n",connStatus);

	switch(connStatus){
	case STATION_IDLE:
		break;
	case STATION_CONNECTING:
		LEDMode = LED_FAST_FLASH;
		os_timer_arm(&tmWifiStat, 1000, 0);
		break;
	case STATION_WRONG_PASSWORD:
		//break;
	case STATION_NO_AP_FOUND:
		//break;
	case STATION_CONNECT_FAIL:
		wifi_station_connect(); //the reconnection mechanism
		os_timer_arm(&tmWifiStat, 1000, 0);
		break;
	case STATION_GOT_IP:
		LEDMode = LED_LIGHT_UP;
		os_timer_arm(&tmWifiStat, 10000, 0);
		if(staGotIPCB){
			staGotIPCB();
		}
		break;
	default:
		break;
	}
}

void ICACHE_FLASH_ATTR
WifiStatusTimerInit(){
    os_timer_disarm(&tmWifiStat);
    os_timer_setfn(&tmWifiStat,(os_timer_func_t *)WifiStatusTimerCB,NULL); //set callback func
    os_timer_arm(&tmWifiStat,100,1); //set timer interval,unit:ms
}

void ICACHE_FLASH_ATTR
WifiEventCB(System_Event_t *argEvent) {
	os_printf("event %x\n", argEvent->event);

	switch (argEvent->event) {
	case EVENT_STAMODE_CONNECTED:
		os_printf("connect to ssid %s, channel %d\n",
				argEvent->event_info.connected.ssid,
				argEvent->event_info.connected.channel);
		break;
	case EVENT_STAMODE_DISCONNECTED:
		os_printf("disconnect from ssid %s, reason %d\n",
				argEvent->event_info.disconnected.ssid,
				argEvent->event_info.disconnected.reason);
		break;
	case EVENT_STAMODE_AUTHMODE_CHANGE:
		os_printf("mode: %d -> %d\n", argEvent->event_info.auth_change.old_mode,
				argEvent->event_info.auth_change.new_mode);
		break;
	case EVENT_STAMODE_GOT_IP:
		os_printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
				IP2STR(&argEvent->event_info.got_ip.ip),
				IP2STR(&argEvent->event_info.got_ip.mask),
				IP2STR(&argEvent->event_info.got_ip.gw));
		os_printf("\n");
		break;
	case EVENT_SOFTAPMODE_STACONNECTED:
		os_printf("station: " MACSTR "join, AID = %d\n",
				MAC2STR(argEvent->event_info.sta_connected.mac),
				argEvent->event_info.sta_connected.aid);
		break;
	case EVENT_SOFTAPMODE_STADISCONNECTED:
		os_printf("station: " MACSTR "leave, AID = %d\n",
				MAC2STR(argEvent->event_info.sta_disconnected.mac),
				argEvent->event_info.sta_disconnected.aid);
		break;
	default:
		break;
	}
}

//call this after flash protect parameters has been loaded, after func: bool LoadFlashProtParam();
void ICACHE_FLASH_ATTR
WifiInitConfig(void)
{
	uint8 WifiMode = wifi_get_opmode_default();
	bool  modeChanged = false;
	if((stFlashProtParam.WorkStatus & WEB_SERV_BIT) || !(stFlashProtParam.WorkStatus & WIFI_CONF_BIT)){
		if(!(WifiMode & SOFTAP_MODE)){
			WifiMode = WifiMode | SOFTAP_MODE;
			modeChanged = true;
		}
	}
	if(stFlashProtParam.WorkStatus & TCP_SERV_BIT){
	}
	if((stFlashProtParam.WorkStatus & MQTT_CLIENT_BIT) && (stFlashProtParam.WorkStatus & MQTT_CONF_BIT) ){
		if(!(WifiMode & STATION_MODE)){
			WifiMode = WifiMode | STATION_MODE;
			modeChanged = true;
		}
	}
	if(modeChanged){
		wifi_set_opmode(WifiMode);
	}
	if(WifiMode & STATION_MODE){
		wifi_station_connect();
		WifiStatusTimerInit();
	}
	TRACE("wifi init,mode:%d\r\n",WifiMode);
	//wifi_set_event_handler_cb(WifiEventCB);
}

bool ICACHE_FLASH_ATTR
WifiConnParamVarify(struct station_config *config){
	return true;
}

bool ICACHE_FLASH_ATTR
WifiStationConfig(struct station_config *config){
	TRACE("station conf ssid:%s\r\npasswd:%s\r\n",config->ssid,config->password);
	uint8 WifiMode = wifi_get_opmode_default();
	if(!(WifiMode & STATION_MODE)){
		WifiMode |= STATION_MODE;
		wifi_set_opmode(WifiMode);
	}
	wifi_station_set_config(config);
	WifiStatusTimerInit();
	return wifi_station_connect();
}

bool ICACHE_FLASH_ATTR
WifiSoftAPConfig(struct softap_config *config ){
	TRACE("softap config\r\n");
	uint8 WifiMode = wifi_get_opmode_default();
	if(!(WifiMode & SOFTAP_MODE)){
		WifiMode = WifiMode | SOFTAP_MODE;
		wifi_set_opmode(WifiMode);
	}
	wifi_softap_set_config(config);// Set ESP8266 softap config, ensure to call this func after softap enabled
	return true;
}
