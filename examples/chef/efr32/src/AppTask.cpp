/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Google LLC.
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "AppTask.h"
#include "AppConfig.h"
#include "AppEvent.h"
#include "LEDWidget.h"
#include "sl_simple_led_instances.h"
#include "HallSensor.h"

#ifdef DISPLAY_ENABLED
#include "lcd.h"
#ifdef QR_CODE_ENABLED
#include "qrcodegen.h"
#endif // QR_CODE_ENABLED
#endif // DISPLAY_ENABLED

#include <app-common/zap-generated/attribute-id.h>
#include <app-common/zap-generated/attribute-type.h>
#include <app-common/zap-generated/cluster-id.h>
#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>

#ifdef EMBER_AF_PLUGIN_IDENTIFY_SERVER
#include <app/clusters/identify-server/identify-server.h>
#endif

#include <assert.h>

#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

#include <platform/EFR32/freertos_bluetooth.h>

#include <lib/support/CodeUtils.h>

#include <platform/CHIPDeviceLayer.h>

#include <app-common/zap-generated/attributes/Accessors.h>

#include <sl_power_manager.h>

#define SYSTEM_STATE_LED &sl_led_led0
#define APP_FUNCTION_BUTTON &sl_button_btn0
#define SET_THRESHOLD_BUTTON &sl_button_btn1

using namespace chip;
using namespace ::chip::DeviceLayer;

//static int idleCount = 0;
//static int idleCount2 = 0;
extern "C" {
extern void XXXTraceIdle()
{
//    ++idleCount;
}
extern void XXXTraceIdle2()
{
//    ++idleCount2;
}
}

#if 0
#define history_len (10)
static sl_power_manager_em_t to_history[history_len];
static sl_power_manager_em_t from_history[history_len];
static int history_index = 0;
static void power_trans_callback(sl_power_manager_em_t from, sl_power_manager_em_t to)
{
    to_history[history_index] = to;
    from_history[history_index] = from;
    history_index = (history_index+1) % history_len;
}

static void log_power_trans()
{
    u_int32_t to = 0;
    u_int32_t from = 0;
    for (int i = 0; i < history_len; i++)
    {
        int j = (history_index + i) % history_len; 
        to = to*10 + to_history[j];
        from = from*10 + from_history[j];
    }

    EFR32_LOG("power hist %d %d", from, to);
}
#endif
 
namespace {
    
TimerHandle_t sHallButtonTimer;

static float GetHallValue()
{
    //EFR32_LOG("idleCount = %d %d", idleCount, idleCount2);
    //log_power_trans();

#if 0
    TaskStatus_t tasks[10];
    uint32_t runtime;

    UBaseType_t result = uxTaskGetSystemState( tasks, 10, &runtime);
    EFR32_LOG("Result= %d %d", result, runtime);
    for (UBaseType_t i = 0; i < result; i++)
    {
        EFR32_LOG("%s %d", tasks[i].pcTaskName, tasks[i].ulRunTimeCounter);
    }
#endif

    float value;
    sl_status_t status = HallSensor::Measure(&value);
    if (status != SL_STATUS_OK)
    {
        EFR32_LOG("HallSensor::Measure error = %d", status);
        return 0.0;
    }
    EFR32_LOG("HallSensor::Measure value = %d", (int)(1000 * value));
    return value;
}

static void HallStateHandler(AppEvent *event)
{
    bool contact_closed = !event->HallStateEvent.State;
    EFR32_LOG("HallState = %d", contact_closed);

    if (PlatformMgr().TryLockChipStack())
    {
        EmberAfStatus status = app::Clusters::BooleanState::Attributes::StateValue::Set(1, contact_closed);
        EFR32_LOG("HallState status = %d", status);
        PlatformMgr().UnlockChipStack();
    }
    else
    {
        EFR32_LOG("HallState failed to lock stack");
    }
}

static void HallButtonHandler(AppEvent *event)
{
    // Handler for Hall button press and release
    // -press starts a timer if not already started
    // -if release occurs before timer expires we perform a hall measurement
    //  and stop the button timer
    // -if timer expires we perform a hall measurement and set the hall threshold
    //  to the value (see HallButtonTimerHandler)

    bool timerActive = xTimerIsTimerActive(sHallButtonTimer) != pdFALSE;

    switch(event->Type)
    {
    case AppEvent::kEventType_Hall_Button_Press:
        if (!timerActive)
        {
            xTimerStart(sHallButtonTimer, 0);
        }
        break;

    case AppEvent::kEventType_Hall_Button_Release:
        if (timerActive)
        {
            xTimerStop(sHallButtonTimer, 0);
            GetHallValue();
        }
        break;
        
    default:
        EFR32_LOG("Unknown hall button event");
        break;
    }
}

static void HallButtonTimerHandler(TimerHandle_t xTimer)
{
    static float min_threshold = 0.300;
    float value = GetHallValue();
    if (value < 0)
        value = -value;
    if (value < min_threshold)
        value = min_threshold;

    EFR32_LOG("Setting hall threshold to %d", (int)(value * 1000));
    HallSensor::SetThreshold(value);
}

#ifdef EMBER_AF_PLUGIN_IDENTIFY_SERVER
EmberAfIdentifyEffectIdentifier sIdentifyEffect = EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_STOP_EFFECT;
#endif // EMBER_AF_PLUGIN_IDENTIFY_SERVER

namespace {
#ifdef EMBER_AF_PLUGIN_IDENTIFY_SERVER
void OnTriggerIdentifyEffectCompleted(chip::System::Layer * systemLayer, void * appState)
{
    ChipLogProgress(Zcl, "Trigger Identify Complete");
    sIdentifyEffect = EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_STOP_EFFECT;

#if CHIP_DEVICE_CONFIG_ENABLE_SED == 1
    AppTask::GetAppTask().StopStatusLEDTimer();
#endif
}
#endif // EMBER_AF_PLUGIN_IDENTIFY_SERVER
} // namespace

#ifdef EMBER_AF_PLUGIN_IDENTIFY_SERVER
void OnTriggerIdentifyEffect(Identify * identify)
{
    sIdentifyEffect = identify->mCurrentEffectIdentifier;

    if (identify->mCurrentEffectIdentifier == EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_CHANNEL_CHANGE)
    {
        ChipLogProgress(Zcl, "IDENTIFY_EFFECT_IDENTIFIER_CHANNEL_CHANGE - Not supported, use effect varriant %d",
                        identify->mEffectVariant);
        sIdentifyEffect = static_cast<EmberAfIdentifyEffectIdentifier>(identify->mEffectVariant);
    }

#if CHIP_DEVICE_CONFIG_ENABLE_SED == 1
    AppTask::GetAppTask().StartStatusLEDTimer();
#endif
    switch (sIdentifyEffect)
    {
    case EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_BLINK:
    case EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_BREATHE:
    case EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_OKAY:
        (void) chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds16(5), OnTriggerIdentifyEffectCompleted,
                                                           identify);
        break;
    case EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_FINISH_EFFECT:
        (void) chip::DeviceLayer::SystemLayer().CancelTimer(OnTriggerIdentifyEffectCompleted, identify);
        (void) chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds16(1), OnTriggerIdentifyEffectCompleted,
                                                           identify);
        break;
    case EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_STOP_EFFECT:
        (void) chip::DeviceLayer::SystemLayer().CancelTimer(OnTriggerIdentifyEffectCompleted, identify);
        sIdentifyEffect = EMBER_ZCL_IDENTIFY_EFFECT_IDENTIFIER_STOP_EFFECT;
        break;
    default:
        ChipLogProgress(Zcl, "No identifier effect");
    }
}
#endif // EMBER_AF_PLUGIN_IDENTIFY_SERVER

#ifdef EMBER_AF_PLUGIN_IDENTIFY_SERVER
Identify gIdentify = {
    chip::EndpointId{ 1 },
    AppTask::GetAppTask().OnIdentifyStart,
    AppTask::GetAppTask().OnIdentifyStop,
    EMBER_ZCL_IDENTIFY_IDENTIFY_TYPE_VISIBLE_LED,
    OnTriggerIdentifyEffect,
};
#endif // EMBER_AF_PLUGIN_IDENTIFY_SERVER

} // namespace
using namespace chip::TLV;
using namespace ::chip::DeviceLayer;

AppTask AppTask::sAppTask;

CHIP_ERROR AppTask::Init()
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    err = BaseApplication::Init(&gIdentify);
    if (err != CHIP_NO_ERROR)
    {
        EFR32_LOG("BaseApplication::Init() failed");
        appError(err);
    }

    // Create Timer for Hall sensor processing
    sHallButtonTimer = xTimerCreate("HallTmr",            // Text Name
                               5000,                    // Default timer period (mS)
                               false,                  // reload timer
                               (void *) this,         // Timer context passed to handler
                               HallButtonTimerHandler // Timer callback handler
    );

    if (sHallButtonTimer == NULL)
    {
        EFR32_LOG("hall timer create failed");
        appError(APP_ERROR_CREATE_TIMER_FAILED);
    }

    sl_status_t status = HallSensor::Init();
    EFR32_LOG("HallSensor::Init %d", status);

#define EM_EVENT_MASK_ALL      (  SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM0 \
                                  | SL_POWER_MANAGER_EVENT_TRANSITION_LEAVING_EM0  \
                                  | SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM1 \
                                  | SL_POWER_MANAGER_EVENT_TRANSITION_LEAVING_EM1  \
                                  | SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM2 \
                                  | SL_POWER_MANAGER_EVENT_TRANSITION_LEAVING_EM2  \
                                  | SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM3 \
                                  | SL_POWER_MANAGER_EVENT_TRANSITION_LEAVING_EM3)
 
    //static sl_power_manager_em_transition_event_handle_t event_handle;
    //static sl_power_manager_em_transition_event_info_t event_info = {
    //    .event_mask = EM_EVENT_MASK_ALL,
    //    .on_event = power_trans_callback,
    //};
 
    //sl_power_manager_subscribe_em_transition_event(&event_handle, &event_info);
    sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);

    return err;
}

CHIP_ERROR AppTask::StartAppTask()
{
    return BaseApplication::StartAppTask(AppTaskMain);
}

void AppTask::AppTaskMain(void * pvParameter)
{
    AppEvent event;
    QueueHandle_t sAppEventQueue = *(static_cast<QueueHandle_t *>(pvParameter));

    CHIP_ERROR err = sAppTask.Init();
    if (err != CHIP_NO_ERROR)
    {
        EFR32_LOG("AppTask.Init() failed");
        appError(err);
    }

#if !(defined(CHIP_DEVICE_CONFIG_ENABLE_SED) && CHIP_DEVICE_CONFIG_ENABLE_SED)
    sAppTask.StartStatusLEDTimer();
#endif
    EFR32_LOG("App Task started");

    while (true)
    {
        BaseType_t eventReceived = xQueueReceive(sAppEventQueue, &event, pdMS_TO_TICKS(1000));
        while (eventReceived == pdTRUE)
        {
            sAppTask.DispatchEvent(&event);
            eventReceived = xQueueReceive(sAppEventQueue, &event, 0);
        }
    }
}

void AppTask::OnIdentifyStart(Identify * identify)
{
    ChipLogProgress(Zcl, "onIdentifyStart");

#if CHIP_DEVICE_CONFIG_ENABLE_SED == 1
    sAppTask.StartStatusLEDTimer();
#endif
}

void AppTask::OnIdentifyStop(Identify * identify)
{
    ChipLogProgress(Zcl, "onIdentifyStop");

#if CHIP_DEVICE_CONFIG_ENABLE_SED == 1
    sAppTask.StopStatusLEDTimer();
#endif
}
void AppTask::ButtonEventHandler(const sl_button_t * buttonHandle, uint8_t btnAction)
{
    if (buttonHandle == NULL)
    {
        return;
    }

    AppEvent button_event           = {};
    button_event.Type               = AppEvent::kEventType_Button;
    button_event.ButtonEvent.Action = btnAction;

    if (buttonHandle == SET_THRESHOLD_BUTTON)
    {
        // Post message to start or stop set hall threshold timer
        // If button is held for timer duration the timer will execute
        AppEvent event;
        event.Handler            = HallButtonHandler;
        event.Type = btnAction == SL_SIMPLE_BUTTON_PRESSED ?
          AppEvent::kEventType_Hall_Button_Press : AppEvent::kEventType_Hall_Button_Release;
        sAppTask.PostEvent(&event);
        return;
    }

    if (buttonHandle == APP_FUNCTION_BUTTON)
    {
        button_event.Handler = BaseApplication::ButtonHandler;
        sAppTask.PostEvent(&button_event);
    }
}

void AppTask::PostHallStateEvent(bool state)
{
    AppEvent event;
    event.Type                 = AppEvent::kEventType_Hall_State_Change;
    event.HallStateEvent.State = state;
    event.Handler              = HallStateHandler;
    sAppTask.PostEvent(&event);
}
