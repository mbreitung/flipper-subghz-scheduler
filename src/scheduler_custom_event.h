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
    SchedulerEventTxDone, // TX thread finished; cleanup runs on the GUI thread
} SchedulerCustomEvent;
