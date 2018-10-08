#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "gpio.h"
#include "user_interface.h"
#include "eagle_soc.h"
#include "UserIO.h"
#include "AlarmClock.h"
#include "GlobalInfo.h"
#include "FlashParam.h"

LOCAL bool bAlarmEnable = true;
LOCAL os_timer_t tmAlarmClk;
//LOCAL STAlarm AlarmQueue[MAX_ALARM_NUM];
extern struct FlashProtectParam stFlashProtParam;
LOCAL uint32 timeStamp = 0;

void ICACHE_FLASH_ATTR
AlarmTimerCB(){
	LOCAL uint32 sntpQueryCtn = 0;
	if(sntpQueryCtn == 0 || timeStamp==0){
		uint32 timeStampTmp;
		timeStampTmp =	sntp_get_current_timestamp();
		if(timeStampTmp !=0){
			timeStamp = timeStampTmp;
			//9-10 0:0:0 -- 17784 days= 1,536,451,200s
			TRACE("sntp time stamp:%d,%s\n",timeStamp,sntp_get_real_time(timeStamp));
		}else if(timeStamp> SNTP_QUERY_INTVL * 2){
			timeStamp += ALARM_CHK_INTVL/1000;
		}
	}else if(timeStamp> SNTP_QUERY_INTVL * 2){
		timeStamp += ALARM_CHK_INTVL/1000;
	}
	os_printf("time stamp:%d,%s\r\n",timeStamp,sntp_get_real_time(timeStamp));

	if(timeStamp>0 && bAlarmEnable){
		bool bRun = false;
		int i=0;
		for(i=0;i<MAX_ALARM_NUM;i++){

			TRACE("alarm id:%d:%d:%d:%d:%d\r\n",stFlashProtParam.AlarmQueue[i].ID,
					stFlashProtParam.AlarmQueue[i].RptMode,
					stFlashProtParam.AlarmQueue[i].Start,
					stFlashProtParam.AlarmQueue[i].Stop,
					stFlashProtParam.AlarmQueue[i].RptIntvl);

			if(stFlashProtParam.AlarmQueue[i].RptMode == RPT_ONLY_ONCE ){
				if(timeStamp >= stFlashProtParam.AlarmQueue[i].Start && timeStamp < stFlashProtParam.AlarmQueue[i].Stop){
					bRun = true;
				}else if(timeStamp >= stFlashProtParam.AlarmQueue[i].Stop && timeStamp <= stFlashProtParam.AlarmQueue[i].Stop + ALARM_EXPIRE){
					stFlashProtParam.AlarmQueue[i].RptMode = 0-RPT_ONLY_ONCE;
					SaveFlashProtParam();
				}
			}else if(stFlashProtParam.AlarmQueue[i].RptMode > 0 && stFlashProtParam.AlarmQueue[i].RptMode <= DAYS_OF_WEEK){
				if(timeStamp >= stFlashProtParam.AlarmQueue[i].Start && timeStamp < stFlashProtParam.AlarmQueue[i].Stop){
					bRun = true;
				}else if(timeStamp >= stFlashProtParam.AlarmQueue[i].Stop){// && timeStamp <= stFlashProtParam.AlarmQueue[i].Stop + ALARM_EXPIRE){
					switch(stFlashProtParam.AlarmQueue[i].RptMode){
					case EVERY_HOUR:
						stFlashProtParam.AlarmQueue[i].Start += 3600;
						stFlashProtParam.AlarmQueue[i].Stop += 3600;
						break;
					case EVERY_DAY:
						stFlashProtParam.AlarmQueue[i].Start += 86400;
						stFlashProtParam.AlarmQueue[i].Stop += 86400;
						break;
					case EVERY_WEEK:
						stFlashProtParam.AlarmQueue[i].Start += 86400*7;
						stFlashProtParam.AlarmQueue[i].Stop += 86400*7;
						break;
					case EVERY_MONTH:
						break;
					case EVERY_YEAR:
						break;
					case INTVL_SECS:
						stFlashProtParam.AlarmQueue[i].Start += stFlashProtParam.AlarmQueue[i].RptIntvl;
						stFlashProtParam.AlarmQueue[i].Stop += stFlashProtParam.AlarmQueue[i].RptIntvl;
						break;
					case INTVL_MINS:
						stFlashProtParam.AlarmQueue[i].Start += stFlashProtParam.AlarmQueue[i].RptIntvl * 60;
						stFlashProtParam.AlarmQueue[i].Stop += stFlashProtParam.AlarmQueue[i].RptIntvl * 60;
						break;
					case INTVL_HOURS:
						stFlashProtParam.AlarmQueue[i].Start += stFlashProtParam.AlarmQueue[i].RptIntvl * 3600;
						stFlashProtParam.AlarmQueue[i].Stop += stFlashProtParam.AlarmQueue[i].RptIntvl * 3600;
						break;
					case INTVL_DAYS:
						stFlashProtParam.AlarmQueue[i].Start += stFlashProtParam.AlarmQueue[i].RptIntvl * 86400;
						stFlashProtParam.AlarmQueue[i].Stop += stFlashProtParam.AlarmQueue[i].RptIntvl * 86400;
						break;
					case DAYS_OF_WEEK:
						break;
					default:
						break;
					}
					SaveFlashProtParam();
				}
			}
		}
		if(bRun){
			RelayOn();
		}else{
			RelayOff();
		}
	}

	sntpQueryCtn++;
	sntpQueryCtn %= SNTP_QUERY_INTVL;
}

bool ICACHE_FLASH_ATTR
SetAlarm(STAlarm* stAlrm){
	if(stAlrm->ID<0 || stAlrm->ID>=MAX_ALARM_NUM){
		return false;
	}else if(stAlrm->Start >= stAlrm->Stop){
		return false;
	}else{
		uint32 timeStampTmp;
		timeStampTmp =	timeStamp;
		if(timeStampTmp !=0){
			int bBreak = false;
			if(stAlrm->Stop < timeStampTmp && !bBreak){
				uint32 add = 0;
				switch(stAlrm->RptMode){
				case EVERY_HOUR:
					add = ((timeStampTmp-stAlrm->Stop+1)/3600+1)*3600;
					stAlrm->Start += add;
					stAlrm->Stop += add;
					break;
				case EVERY_DAY:
					add = ((timeStampTmp-stAlrm->Stop+1)/86400+1)*86400;
					stAlrm->Start += add;
					stAlrm->Stop += add;
					break;
				case EVERY_WEEK:
					add = ((timeStampTmp-stAlrm->Stop+1)/(86400*7)+1)*86400*7;
					stAlrm->Start += add;
					stAlrm->Stop += add;
					break;
				case EVERY_MONTH:
					bBreak = true;
					break;
				case EVERY_YEAR:
					bBreak = true;
					break;
				case INTVL_SECS:
					add =  (((timeStampTmp-stAlrm->Stop+1)/(stAlrm->RptIntvl))+1)*stAlrm->RptIntvl;
					stAlrm->Start += add;
					stAlrm->Stop += add;
					break;
				case INTVL_MINS:
					add = (((timeStampTmp-stAlrm->Stop+1)/(stAlrm->RptIntvl * 60))+1)*stAlrm->RptIntvl * 60;
					stAlrm->Start += add;
					stAlrm->Stop += add;
					break;
				case INTVL_HOURS:
					add = (((timeStampTmp-stAlrm->Stop+1)/(stAlrm->RptIntvl * 3600))+1)*stAlrm->RptIntvl * 3600;
					stAlrm->Start += add;
					stAlrm->Stop += add;
					break;
				case INTVL_DAYS:
					add = (((timeStampTmp-stAlrm->Stop+1)/(stAlrm->RptIntvl * 86400))+1)*stAlrm->RptIntvl * 86400;
					stAlrm->Start += add;
					stAlrm->Stop += add;
					break;
				case DAYS_OF_WEEK:
					bBreak = true;
					break;
				default:
					bBreak = true;
					break;
				}
			}
		}
		os_memcpy(&(stFlashProtParam.AlarmQueue[stAlrm->ID]),stAlrm,sizeof(STAlarm));
		SaveFlashProtParam();
		return true;
	}
}

void EnableAllAlarms(){
	bAlarmEnable = true;
}

void DisableAllAlarms(){
	bAlarmEnable = false;
}

bool ICACHE_FLASH_ATTR
EnableAlarm(uint8 ID){
	if(ID<0 || ID>=MAX_ALARM_NUM){
		return false;
	}
	if(stFlashProtParam.AlarmQueue[ID].RptMode < 0){
		stFlashProtParam.AlarmQueue[ID].RptMode = 0-stFlashProtParam.AlarmQueue[ID].RptMode;
	}
	SaveFlashProtParam();
	return true;
}

bool ICACHE_FLASH_ATTR
DisableAlarm(uint8 ID){
	if(ID<0 || ID>=MAX_ALARM_NUM){
		return false;
	}
	if(stFlashProtParam.AlarmQueue[ID].RptMode > 0){
		stFlashProtParam.AlarmQueue[ID].RptMode = 0-stFlashProtParam.AlarmQueue[ID].RptMode;
	}
	SaveFlashProtParam();
	return true;
}

void ICACHE_FLASH_ATTR
AlarmTimerInit(){
    os_timer_disarm(&tmAlarmClk);
    os_timer_setfn(&tmAlarmClk,(os_timer_func_t *)AlarmTimerCB,NULL); //set callback func
    os_timer_arm(&tmAlarmClk,ALARM_CHK_INTVL,1); //set timer interval,unit:ms
    bAlarmEnable = true;
    timeStamp = 0;
}
