#pragma once

typedef enum {
    SchedulerStartEventSetInterval,
    SchedulerStartEventSetTiming,
    SchedulerStartEventSetTxCount,
    SchedulerStartEventSetTxLoops,
    SchedulerStartEventSetImmediate,
    SchedulerStartEventSetTxDelay,
    SchedulerStartEventSetRadio,
    SchedulerStartEventSelectFile,
    SchedulerStartEventSelectRandomFile,
    SchedulerStartEventSaveSchedule,
    SchedulerStartRunEvent,
    SchedulerEventPopupDone,
    SchedulerCustomEventErrorBack,
} SchedulerCustomEvent;
