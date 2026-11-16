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
    SchedulerStartEventSaveSchedule,
    SchedulerStartRunEvent,
    SchedulerEventPopupDone,
    SchedulerCustomEventErrorBack,
} SchedulerCustomEvent;
