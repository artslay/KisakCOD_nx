#pragma once
#include <cstdint>
#include <universal/timing.h>
enum SwitchMainFrameStage : uint32_t {
 SWITCH_MAIN_COM_FRAME_TOTAL=0, SWITCH_MAIN_FRAME_TOTAL, SWITCH_MAIN_EVENT_LOOP,
 SWITCH_MAIN_FRAME_PACING_SLEEP, SWITCH_MAIN_COMMANDS, SWITCH_MAIN_SERVER,
 SWITCH_MAIN_PREFRAME, SWITCH_MAIN_CLIENT, SWITCH_MAIN_SCREEN, SWITCH_MAIN_RAGDOLL,
 SWITCH_MAIN_END_WAIT, SWITCH_MAIN_DB_UPDATE, SWITCH_MAIN_FRAME_STAGE_COUNT
};
extern "C" bool Switch_PerfDiagEnabled();
extern "C" void Switch_RecordMainFrameStage(uint32_t stage,uint64_t elapsedNs);
extern "C" void Switch_LogThreadDiagSummary();
extern "C" void Switch_LogWorkerDiagSummary();
class SwitchPerfDiagScope {
public:
 explicit SwitchPerfDiagScope(uint32_t stage):m_stage(stage),m_enabled(Switch_PerfDiagEnabled()),m_start(m_enabled?Kisak_RawTimerTicks():0){}
 ~SwitchPerfDiagScope(){if(m_enabled)Switch_RecordMainFrameStage(m_stage,Kisak_RawTimerTicks()-m_start);}
 SwitchPerfDiagScope(const SwitchPerfDiagScope&)=delete;
 SwitchPerfDiagScope& operator=(const SwitchPerfDiagScope&)=delete;
private:uint32_t m_stage;bool m_enabled;uint64_t m_start;
};
